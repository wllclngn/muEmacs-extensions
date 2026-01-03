# NEUROXUS Agent Orchestration - Crystal Port
#
# Spawns and manages AI agents (Claude CLI) as child processes,
# captures output via pipes, provides polling interface for editor.

require "atomic"

# Agent type (Claude only for now)
enum AgentType
  Claude
end

# Agent lifecycle status
enum AgentStatus
  Idle
  Spawning
  Running
  Completed
  Failed
  Killed
end

# Agent handle
class Agent
  MAX_OUTPUT = 1024 * 1024  # 1MB max output

  property id : UInt32
  property pid : Int64
  property type : AgentType
  property name : String
  property output : String
  property read_pos : Int32
  property exit_code : Int32
  property buffer_name : String

  @status : Atomic(Int32)
  @process : Process?
  @reader_fiber : Fiber?
  @output_lock : Mutex

  def initialize(@id, @type)
    @pid = 0_i64
    @name = "claude-#{@id}"
    @buffer_name = "*claude-#{@id}*"
    @output = ""
    @read_pos = 0
    @exit_code = 0
    @status = Atomic(Int32).new(AgentStatus::Idle.value)
    @process = nil
    @reader_fiber = nil
    @output_lock = Mutex.new
  end

  def status : AgentStatus
    AgentStatus.from_value(@status.get)
  end

  def status=(s : AgentStatus)
    @status.set(s.value)
  end

  def running? : Bool
    s = status
    s == AgentStatus::Spawning || s == AgentStatus::Running
  end

  # Spawn the agent with given prompt
  def spawn(prompt : String) : Bool
    return false if running?

    self.status = AgentStatus::Spawning
    @output = ""
    @read_pos = 0
    @exit_code = 0

    begin
      @process = Process.new(
        "claude",
        ["-p", prompt, "--dangerously-skip-permissions"],
        output: Process::Redirect::Pipe,
        error: Process::Redirect::Pipe
      )

      @pid = @process.not_nil!.pid.to_i64
      self.status = AgentStatus::Running

      # Start reader fiber
      start_reader
      true
    rescue ex
      self.status = AgentStatus::Failed
      @output = "Failed to spawn: #{ex.message}"
      false
    end
  end

  # Start fiber to read process output
  private def start_reader
    process = @process
    return unless process

    @reader_fiber = spawn do
      stdout = process.output
      stderr = process.error

      # Read both stdout and stderr
      loop do
        break unless running?

        # Non-blocking read attempt
        begin
          if stdout.peek && stdout.peek.not_nil!.size > 0
            data = stdout.gets(chomp: false)
            if data
              @output_lock.synchronize do
                if @output.bytesize + data.bytesize < MAX_OUTPUT
                  @output += data
                end
              end
            end
          end
        rescue IO::Error
          break
        end

        # Small sleep to prevent busy loop
        sleep 10.milliseconds
      end

      # Process completed, get exit status
      exit_status = process.wait
      @exit_code = exit_status.exit_code

      if exit_status.success?
        self.status = AgentStatus::Completed
      else
        self.status = AgentStatus::Failed
      end
    end
  end

  # Read available output (non-blocking)
  def read(max_len : Int32) : String
    @output_lock.synchronize do
      available = @output.bytesize - @read_pos
      return "" if available <= 0

      to_read = Math.min(available, max_len)
      result = @output[@read_pos, to_read]
      @read_pos += to_read
      result
    end
  end

  # Get all output
  def full_output : String
    @output_lock.synchronize { @output }
  end

  # Kill the agent
  def kill : Bool
    return false unless running?

    process = @process
    return false unless process

    begin
      process.signal(Signal::TERM)
      sleep 100.milliseconds

      # Force kill if still running
      if process.exists?
        process.signal(Signal::KILL)
      end

      self.status = AgentStatus::Killed
      true
    rescue
      false
    end
  end
end

# Agent pool manager
module AgentPool
  MAX_AGENTS = 4

  @@agents = Array(Agent?).new(MAX_AGENTS, nil)
  @@next_id = Atomic(UInt32).new(0_u32)
  @@initialized = false

  # Initialize the pool
  def self.init
    return if @@initialized
    @@agents = Array(Agent?).new(MAX_AGENTS, nil)
    @@next_id.set(0_u32)
    @@initialized = true
  end

  # Shutdown - kill all agents and free resources
  def self.shutdown
    return unless @@initialized

    # Kill all running agents
    @@agents.each do |agent|
      next unless agent
      if agent.running?
        agent.kill
      end
    end

    # Clear agent references
    MAX_AGENTS.times do |i|
      @@agents[i] = nil
    end

    @@initialized = false
  end

  # Find a free slot (or reclaim completed)
  private def self.find_free_slot : Int32?
    MAX_AGENTS.times do |i|
      agent = @@agents[i]

      # Empty slot
      return i if agent.nil?

      # Reclaim completed slot
      s = agent.status
      if s == AgentStatus::Completed || s == AgentStatus::Failed || s == AgentStatus::Killed
        return i
      end
    end
    nil
  end

  # Spawn a new agent
  def self.spawn(prompt : String) : Agent?
    init unless @@initialized

    slot = find_free_slot
    return nil unless slot

    id = @@next_id.add(1_u32) + 1
    agent = Agent.new(id, AgentType::Claude)

    if agent.spawn(prompt)
      @@agents[slot] = agent
      agent
    else
      nil
    end
  end

  # Get agent by ID
  def self.get(id : UInt32) : Agent?
    @@agents.find { |a| a.try &.id == id }
  end

  # Get agent by index
  def self.get_by_index(index : Int32) : Agent?
    return nil if index < 0 || index >= MAX_AGENTS
    @@agents[index]
  end

  # Count running agents
  def self.count_running : Int32
    @@agents.count { |a| a.try &.running? }
  end

  # List all agents
  def self.list : Array(Agent)
    @@agents.compact
  end

  # Poll all agents (for editor integration)
  def self.poll_all(&block : Agent, String, Bool ->)
    @@agents.each do |agent|
      next unless agent

      output = agent.read(4096)
      completed = !agent.running?

      if output.size > 0 || completed
        block.call(agent, output, completed)
      end
    end
  end

  # Kill agent by ID
  def self.kill(id : UInt32) : Bool
    agent = get(id)
    return false unless agent
    agent.kill
  end
end
