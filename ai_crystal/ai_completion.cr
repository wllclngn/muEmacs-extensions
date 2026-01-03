# NEUROXUS AI Agent Extension for uEmacs
# Crystal implementation - spawns Claude CLI agents
#
# Commands:
#   ai-spawn   - Spawn claude with prompt, output to *claude-N* buffer
#   ai-status  - List all agents and their status
#   ai-output  - Switch to agent's output buffer
#   ai-kill    - Kill a running agent
#   ai-poll    - Poll agents and update buffers

require "./agent"

# C bridge imports
lib LibBridge
  fun bridge_message(msg : LibC::Char*) : Void
  fun bridge_current_buffer : Void*
  fun bridge_buffer_contents(bp : Void*, len : LibC::SizeT*) : LibC::Char*
  fun bridge_buffer_filename(bp : Void*) : LibC::Char*
  fun bridge_get_point(line : LibC::Int*, col : LibC::Int*) : Void
  fun bridge_buffer_insert(text : LibC::Char*, len : LibC::SizeT) : LibC::Int
  fun bridge_buffer_create(name : LibC::Char*) : Void*
  fun bridge_buffer_switch(bp : Void*) : LibC::Int
  fun bridge_buffer_clear(bp : Void*) : LibC::Int
  fun bridge_prompt(prompt : LibC::Char*, buf : LibC::Char*, max : LibC::Int) : LibC::Int
  fun bridge_free(ptr : Void*) : Void
end

# Helper module for API interaction
module API
  def self.message(msg : String)
    LibBridge.bridge_message(msg.to_unsafe)
  end

  def self.current_buffer : Void*
    LibBridge.bridge_current_buffer
  end

  def self.buffer_contents(bp : Void*) : String
    len = uninitialized LibC::SizeT
    ptr = LibBridge.bridge_buffer_contents(bp, pointerof(len))
    return "" if ptr.null?
    result = String.new(ptr, len)
    LibBridge.bridge_free(ptr.as(Void*))
    result
  end

  def self.buffer_filename(bp : Void*) : String
    ptr = LibBridge.bridge_buffer_filename(bp)
    return "" if ptr.null?
    String.new(ptr)
  end

  def self.get_point : {Int32, Int32}
    line = uninitialized LibC::Int
    col = uninitialized LibC::Int
    LibBridge.bridge_get_point(pointerof(line), pointerof(col))
    {line.to_i32, col.to_i32}
  end

  def self.insert(text : String)
    LibBridge.bridge_buffer_insert(text.to_unsafe, text.bytesize)
  end

  def self.create_buffer(name : String) : Void*
    LibBridge.bridge_buffer_create(name.to_unsafe)
  end

  def self.switch_buffer(bp : Void*) : Bool
    LibBridge.bridge_buffer_switch(bp) != 0
  end

  def self.clear_buffer(bp : Void*)
    LibBridge.bridge_buffer_clear(bp)
  end

  def self.prompt(prompt_text : String, max_len : Int32 = 256) : String?
    buf = Bytes.new(max_len)
    result = LibBridge.bridge_prompt(prompt_text.to_unsafe, buf.to_unsafe.as(LibC::Char*), max_len)
    return nil if result == 0
    String.new(buf.to_unsafe, buf.index(0_u8) || 0)
  end
end

# Show agent output in its dedicated buffer
def show_agent_output(agent : Agent)
  bp = API.create_buffer(agent.buffer_name)
  return unless bp

  API.switch_buffer(bp)
  API.clear_buffer(bp)

  output = agent.full_output
  output.each_line do |line|
    API.insert(line + "\n")
  end
end

# Command: ai-spawn - Spawn claude with prompt
fun crystal_ai_spawn(f : LibC::Int, n : LibC::Int) : LibC::Int
  # Check if we can spawn
  if AgentPool.count_running >= AgentPool::MAX_AGENTS
    API.message("ai-spawn: Max agents (#{AgentPool::MAX_AGENTS}) running")
    return 0
  end

  # Prompt for task
  prompt = API.prompt("Claude task: ")
  if prompt.nil? || prompt.empty?
    API.message("ai-spawn: Cancelled")
    return 0
  end

  API.message("ai-spawn: Spawning claude...")

  agent = AgentPool.spawn(prompt)
  if agent
    API.message("ai-spawn: Started #{agent.name} (pid #{agent.pid})")

    # Create and switch to output buffer
    bp = API.create_buffer(agent.buffer_name)
    if bp
      API.switch_buffer(bp)
      API.clear_buffer(bp)
      API.insert("# Claude Agent #{agent.id}\n")
      API.insert("# Task: #{prompt}\n")
      API.insert("# Status: Running...\n\n")
    end

    return 1
  else
    API.message("ai-spawn: Failed to spawn (is 'claude' installed?)")
    return 0
  end
end

# Command: ai-status - List all agents
fun crystal_ai_status(f : LibC::Int, n : LibC::Int) : LibC::Int
  agents = AgentPool.list

  if agents.empty?
    API.message("ai-status: No agents")
    return 1
  end

  # Create status buffer
  bp = API.create_buffer("*ai-agents*")
  return 0 unless bp

  API.switch_buffer(bp)
  API.clear_buffer(bp)

  API.insert("# AI Agents\n\n")
  API.insert("ID   Status      PID       Output    Buffer\n")
  API.insert("---  ----------  --------  --------  ----------------\n")

  agents.each do |agent|
    status_str = case agent.status
    when AgentStatus::Idle      then "idle"
    when AgentStatus::Spawning  then "spawning"
    when AgentStatus::Running   then "RUNNING"
    when AgentStatus::Completed then "done"
    when AgentStatus::Failed    then "FAILED"
    when AgentStatus::Killed    then "killed"
    else                             "???"
    end

    output_kb = agent.full_output.bytesize / 1024.0
    line = sprintf("%-4d %-10s  %-8d  %6.1fKB  %s\n",
      agent.id, status_str, agent.pid, output_kb, agent.buffer_name)
    API.insert(line)
  end

  API.insert("\n# Commands: ai-output N, ai-kill N, ai-poll\n")

  API.message("ai-status: #{agents.size} agent(s), #{AgentPool.count_running} running")
  return 1
end

# Command: ai-output - Show agent output buffer
fun crystal_ai_output(f : LibC::Int, n : LibC::Int) : LibC::Int
  agents = AgentPool.list

  if agents.empty?
    API.message("ai-output: No agents")
    return 0
  end

  # If only one agent, show it directly
  if agents.size == 1
    show_agent_output(agents.first)
    API.message("ai-output: #{agents.first.name}")
    return 1
  end

  # Prompt for agent ID
  id_str = API.prompt("Agent ID: ")
  return 0 if id_str.nil? || id_str.empty?

  id = id_str.to_u32?
  unless id
    API.message("ai-output: Invalid ID")
    return 0
  end

  agent = AgentPool.get(id)
  unless agent
    API.message("ai-output: Agent #{id} not found")
    return 0
  end

  show_agent_output(agent)
  API.message("ai-output: #{agent.name} (#{agent.status})")
  return 1
end

# Command: ai-kill - Kill a running agent
fun crystal_ai_kill(f : LibC::Int, n : LibC::Int) : LibC::Int
  running = AgentPool.list.select(&.running?)

  if running.empty?
    API.message("ai-kill: No running agents")
    return 0
  end

  # If only one running, kill it directly
  if running.size == 1
    agent = running.first
    if agent.kill
      API.message("ai-kill: Killed #{agent.name}")
      return 1
    else
      API.message("ai-kill: Failed to kill #{agent.name}")
      return 0
    end
  end

  # Prompt for agent ID
  id_str = API.prompt("Kill agent ID: ")
  return 0 if id_str.nil? || id_str.empty?

  id = id_str.to_u32?
  unless id
    API.message("ai-kill: Invalid ID")
    return 0
  end

  if AgentPool.kill(id)
    API.message("ai-kill: Killed agent #{id}")
    return 1
  else
    API.message("ai-kill: Failed to kill agent #{id}")
    return 0
  end
end

# Command: ai-poll - Poll agents and update buffers
fun crystal_ai_poll(f : LibC::Int, n : LibC::Int) : LibC::Int
  updated = 0

  AgentPool.poll_all do |agent, output, completed|
    next if output.empty? && !completed

    # Update agent's buffer
    bp = API.create_buffer(agent.buffer_name)
    if bp
      # Append new output (don't clear)
      current_bp = API.current_buffer
      API.switch_buffer(bp)
      API.insert(output) unless output.empty?

      if completed
        API.insert("\n\n# --- Agent #{agent.status} (exit code: #{agent.exit_code}) ---\n")
      end

      API.switch_buffer(current_bp) if current_bp
    end

    updated += 1
  end

  if updated > 0
    API.message("ai-poll: Updated #{updated} agent(s)")
  else
    running = AgentPool.count_running
    if running > 0
      API.message("ai-poll: #{running} running, no new output")
    else
      API.message("ai-poll: No agents")
    end
  end

  return 1
end

# Legacy compatibility: ai-complete using agent system
fun crystal_ai_complete(f : LibC::Int, n : LibC::Int) : LibC::Int
  bp = API.current_buffer
  if bp.null?
    API.message("ai-complete: No buffer")
    return 0
  end

  content = API.buffer_contents(bp)
  if content.empty?
    API.message("ai-complete: No content")
    return 0
  end

  line, col = API.get_point
  lines = content.split('\n')
  current_line = lines[line - 1]? || ""

  # Get context (5 lines before and after)
  start_idx = Math.max(0, line - 6)
  end_idx = Math.min(lines.size - 1, line + 4)
  context = lines[start_idx..end_idx].join('\n')

  prompt = "Complete this code. Only output the completion, no explanation:\n\n#{context}\n\nComplete after: #{current_line}"

  API.message("ai-complete: Spawning claude...")

  agent = AgentPool.spawn(prompt)
  if agent
    API.message("ai-complete: Started #{agent.name} - use ai-poll to check")
    return 1
  else
    API.message("ai-complete: Failed (is 'claude' installed?)")
    return 0
  end
end

# Keep ai-explain for compatibility
fun crystal_ai_explain(f : LibC::Int, n : LibC::Int) : LibC::Int
  bp = API.current_buffer
  if bp.null?
    API.message("ai-explain: No buffer")
    return 0
  end

  content = API.buffer_contents(bp)
  line, _ = API.get_point
  lines = content.split('\n')
  current_line = lines[line - 1]? || ""

  if current_line.empty?
    API.message("ai-explain: Empty line")
    return 0
  end

  prompt = "Explain this code in one short sentence:\n#{current_line}"

  agent = AgentPool.spawn(prompt)
  if agent
    API.message("ai-explain: Started #{agent.name}")
    return 1
  else
    API.message("ai-explain: Failed to spawn")
    return 0
  end
end

# Keep ai-fix for compatibility
fun crystal_ai_fix(f : LibC::Int, n : LibC::Int) : LibC::Int
  bp = API.current_buffer
  if bp.null?
    API.message("ai-fix: No buffer")
    return 0
  end

  content = API.buffer_contents(bp)
  line, _ = API.get_point
  lines = content.split('\n')
  current_line = lines[line - 1]? || ""

  if current_line.empty?
    API.message("ai-fix: Empty line")
    return 0
  end

  filename = API.buffer_filename(bp)
  ext = filename.split('.').last? || ""

  prompt = "Find any bugs or issues in this #{ext} code and suggest a one-line fix:\n#{current_line}"

  agent = AgentPool.spawn(prompt)
  if agent
    API.message("ai-fix: Started #{agent.name}")
    return 1
  else
    API.message("ai-fix: Failed to spawn")
    return 0
  end
end

# Cleanup function called by C bridge on extension unload
fun crystal_cleanup : Void
  AgentPool.shutdown
end
