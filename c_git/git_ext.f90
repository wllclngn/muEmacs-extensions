! Git Integration Extension for uEmacs
! The actual command logic in Fortran, with C bridge for API access

module git_commands
    use, intrinsic :: iso_c_binding
    implicit none

    ! C bridge functions
    interface
        subroutine bridge_message(msg) bind(C, name='bridge_message')
            import :: c_char
            character(kind=c_char), intent(in) :: msg(*)
        end subroutine bridge_message

        function bridge_current_buffer() bind(C, name='bridge_current_buffer') result(bp)
            import :: c_ptr
            type(c_ptr) :: bp
        end function bridge_current_buffer

        function bridge_buffer_filename(bp) bind(C, name='bridge_buffer_filename') result(fn)
            import :: c_ptr
            type(c_ptr), value :: bp
            type(c_ptr) :: fn
        end function bridge_buffer_filename

        function bridge_buffer_create(name) bind(C, name='bridge_buffer_create') result(bp)
            import :: c_ptr, c_char
            character(kind=c_char), intent(in) :: name(*)
            type(c_ptr) :: bp
        end function bridge_buffer_create

        function bridge_buffer_switch(bp) bind(C, name='bridge_buffer_switch') result(r)
            import :: c_ptr, c_int
            type(c_ptr), value :: bp
            integer(c_int) :: r
        end function bridge_buffer_switch

        function bridge_buffer_clear(bp) bind(C, name='bridge_buffer_clear') result(r)
            import :: c_ptr, c_int
            type(c_ptr), value :: bp
            integer(c_int) :: r
        end function bridge_buffer_clear

        function bridge_buffer_insert(text, tlen) bind(C, name='bridge_buffer_insert') result(r)
            import :: c_char, c_size_t, c_int
            character(kind=c_char), intent(in) :: text(*)
            integer(c_size_t), value :: tlen
            integer(c_int) :: r
        end function bridge_buffer_insert

        subroutine bridge_get_point(line, col) bind(C, name='bridge_get_point')
            import :: c_int
            integer(c_int), intent(out) :: line, col
        end subroutine bridge_get_point
    end interface

contains

    ! Convert C string pointer to Fortran string
    function c_to_f_string(cptr) result(fstr)
        type(c_ptr), intent(in) :: cptr
        character(len=512) :: fstr
        character(kind=c_char), pointer :: carr(:)
        integer :: i, n

        fstr = ''
        if (.not. c_associated(cptr)) return

        call c_f_pointer(cptr, carr, [512])
        n = 0
        do i = 1, 512
            if (carr(i) == c_null_char) exit
            n = i
        end do
        do i = 1, n
            fstr(i:i) = carr(i)
        end do
    end function c_to_f_string

    ! Execute shell command, return output
    function exec_cmd(cmd) result(output)
        character(len=*), intent(in) :: cmd
        character(len=16384) :: output
        character(len=64) :: tmpfile
        integer :: unit_num, ios, pid
        character(len=1024) :: line

        output = ''

        ! Get unique temp file
        call system_clock(pid)
        write(tmpfile, '(A,I0,A)') '/tmp/fortran_git_', mod(pid, 100000), '.tmp'

        ! Execute command
        call execute_command_line(trim(cmd) // ' > ' // trim(tmpfile) // ' 2>&1', wait=.true.)

        ! Read output
        open(newunit=unit_num, file=trim(tmpfile), status='old', action='read', iostat=ios)
        if (ios /= 0) return

        do
            read(unit_num, '(A)', iostat=ios) line
            if (ios /= 0) exit
            if (len_trim(output) + len_trim(line) + 1 < 16000) then
                output = trim(output) // trim(line) // char(10)
            end if
        end do
        close(unit_num, status='delete')
    end function exec_cmd

    ! Helper to show output in a named buffer
    subroutine show_in_buffer(bufname, content)
        character(len=*), intent(in) :: bufname, content
        type(c_ptr) :: bp
        character(len=256), target :: cbufname
        character(len=4096), target :: ctext
        integer(c_size_t) :: tlen
        integer(c_int) :: dummy
        character(len=1024) :: line
        integer :: i, j, n

        cbufname = trim(bufname) // c_null_char
        bp = bridge_buffer_create(cbufname)
        if (.not. c_associated(bp)) return

        dummy = bridge_buffer_switch(bp)
        dummy = bridge_buffer_clear(bp)

        ! Insert content line by line
        n = len_trim(content)
        i = 1
        do while (i <= n)
            ! Find end of line
            j = i
            do while (j <= n .and. content(j:j) /= char(10))
                j = j + 1
            end do

            ! Insert this line
            if (j > i) then
                line = content(i:j-1)
                ctext = trim(line) // char(10) // c_null_char
                tlen = len_trim(line) + 1
                dummy = bridge_buffer_insert(ctext, tlen)
            else
                ctext = char(10) // c_null_char
                tlen = 1
                dummy = bridge_buffer_insert(ctext, tlen)
            end if
            i = j + 1
        end do
    end subroutine show_in_buffer

    ! Helper to send message
    subroutine msg(text)
        character(len=*), intent(in) :: text
        character(len=256), target :: ctext
        ctext = trim(text) // c_null_char
        call bridge_message(ctext)
    end subroutine msg

end module git_commands

! Command implementations - exported with bind(C)
function fortran_git_status(f, n) bind(C, name='fortran_git_status') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    character(len=16384) :: output

    output = exec_cmd('git status --short 2>/dev/null || echo "Not a git repository"')
    call show_in_buffer('*git-status*', output)
    call msg('git-status')
    ret = 1
end function fortran_git_status

function fortran_git_diff(f, n) bind(C, name='fortran_git_diff') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    character(len=16384) :: output

    output = exec_cmd('git diff 2>/dev/null || echo "Not a git repository"')
    if (len_trim(output) < 2) then
        call msg('git-diff: No changes')
        ret = 1
        return
    end if
    call show_in_buffer('*git-diff*', output)
    call msg('git-diff')
    ret = 1
end function fortran_git_diff

function fortran_git_log(f, n) bind(C, name='fortran_git_log') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    character(len=16384) :: output

    output = exec_cmd('git log --oneline -20 2>/dev/null || echo "Not a git repository"')
    call show_in_buffer('*git-log*', output)
    call msg('git-log: 20 recent commits')
    ret = 1
end function fortran_git_log

function fortran_git_blame(f, n) bind(C, name='fortran_git_blame') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    type(c_ptr) :: bp, fn_ptr
    character(len=512) :: filename
    character(len=16384) :: output
    character(len=600) :: cmd
    integer(c_int) :: line, col

    bp = bridge_current_buffer()
    if (.not. c_associated(bp)) then
        call msg('git-blame: No buffer')
        ret = 0
        return
    end if

    fn_ptr = bridge_buffer_filename(bp)
    filename = c_to_f_string(fn_ptr)
    if (len_trim(filename) == 0) then
        call msg('git-blame: No file')
        ret = 0
        return
    end if

    call bridge_get_point(line, col)

    write(cmd, '(A,I0,A,I0,A)') 'git blame -L', line, ',', line, ' "' // trim(filename) // '" 2>/dev/null'
    output = exec_cmd(cmd)

    if (len_trim(output) > 0) then
        ! Show in message bar (truncate to ~80 chars)
        call msg(trim(output(1:min(len_trim(output), 80))))
    else
        call msg('git-blame: Not tracked')
    end if
    ret = 1
end function fortran_git_blame

function fortran_git_add(f, n) bind(C, name='fortran_git_add') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    type(c_ptr) :: bp, fn_ptr
    character(len=512) :: filename
    character(len=16384) :: output
    character(len=600) :: cmd

    bp = bridge_current_buffer()
    if (.not. c_associated(bp)) then
        call msg('git-add: No buffer')
        ret = 0
        return
    end if

    fn_ptr = bridge_buffer_filename(bp)
    filename = c_to_f_string(fn_ptr)
    if (len_trim(filename) == 0) then
        call msg('git-add: No file')
        ret = 0
        return
    end if

    cmd = 'git add "' // trim(filename) // '" 2>&1'
    output = exec_cmd(cmd)

    if (len_trim(output) == 0) then
        call msg('git-add: Staged ' // trim(filename))
    else
        call msg('git-add: ' // trim(output(1:min(len_trim(output), 60))))
    end if
    ret = 1
end function fortran_git_add

function fortran_git_status_full(f, n) bind(C, name='fortran_git_status_full') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    character(len=16384) :: output

    output = exec_cmd('git status 2>/dev/null || echo "Not a git repository"')
    call show_in_buffer('*git-status*', output)
    call msg('git-status (full)')
    ret = 1
end function fortran_git_status_full

function fortran_git_stage(f, n) bind(C, name='fortran_git_stage') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    type(c_ptr) :: bp, fn_ptr
    character(len=512) :: filename
    character(len=16384) :: output
    character(len=600) :: cmd

    bp = bridge_current_buffer()
    if (.not. c_associated(bp)) then
        call msg('git-stage: No buffer')
        ret = 0
        return
    end if

    fn_ptr = bridge_buffer_filename(bp)
    filename = c_to_f_string(fn_ptr)
    if (len_trim(filename) == 0) then
        call msg('git-stage: No file')
        ret = 0
        return
    end if

    cmd = 'git add "' // trim(filename) // '" 2>&1'
    output = exec_cmd(cmd)

    if (len_trim(output) == 0) then
        call msg('git-stage: Staged ' // trim(filename))
    else
        call msg('git-stage: ' // trim(output(1:min(len_trim(output), 60))))
    end if
    ret = 1
end function fortran_git_stage

function fortran_git_unstage(f, n) bind(C, name='fortran_git_unstage') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    type(c_ptr) :: bp, fn_ptr
    character(len=512) :: filename
    character(len=16384) :: output
    character(len=600) :: cmd

    bp = bridge_current_buffer()
    if (.not. c_associated(bp)) then
        call msg('git-unstage: No buffer')
        ret = 0
        return
    end if

    fn_ptr = bridge_buffer_filename(bp)
    filename = c_to_f_string(fn_ptr)
    if (len_trim(filename) == 0) then
        call msg('git-unstage: No file')
        ret = 0
        return
    end if

    cmd = 'git restore --staged "' // trim(filename) // '" 2>&1'
    output = exec_cmd(cmd)

    if (len_trim(output) == 0) then
        call msg('git-unstage: Unstaged ' // trim(filename))
    else
        call msg('git-unstage: ' // trim(output(1:min(len_trim(output), 60))))
    end if
    ret = 1
end function fortran_git_unstage

function fortran_git_commit(f, n) bind(C, name='fortran_git_commit') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    character(len=16384) :: output

    ! Note: Real commit would need message input - this opens editor
    output = exec_cmd('git commit 2>&1')
    if (index(output, 'nothing to commit') > 0) then
        call msg('git-commit: Nothing to commit')
    else if (len_trim(output) > 0) then
        call msg('git-commit: ' // trim(output(1:min(len_trim(output), 60))))
    end if
    ret = 1
end function fortran_git_commit

function fortran_git_pull(f, n) bind(C, name='fortran_git_pull') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    character(len=16384) :: output

    call msg('git-pull: Pulling...')
    output = exec_cmd('git pull 2>&1')
    if (index(output, 'Already up to date') > 0) then
        call msg('git-pull: Already up to date')
    else
        call show_in_buffer('*git-pull*', output)
        call msg('git-pull: Complete')
    end if
    ret = 1
end function fortran_git_pull

function fortran_git_push(f, n) bind(C, name='fortran_git_push') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    character(len=16384) :: output

    call msg('git-push: Pushing...')
    output = exec_cmd('git push 2>&1')
    if (index(output, 'Everything up-to-date') > 0) then
        call msg('git-push: Everything up-to-date')
    else
        call msg('git-push: ' // trim(output(1:min(len_trim(output), 60))))
    end if
    ret = 1
end function fortran_git_push

function fortran_git_branch(f, n) bind(C, name='fortran_git_branch') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    character(len=16384) :: output

    output = exec_cmd('git branch -a 2>/dev/null || echo "Not a git repository"')
    call show_in_buffer('*git-branch*', output)
    call msg('git-branch')
    ret = 1
end function fortran_git_branch

function fortran_git_stash(f, n) bind(C, name='fortran_git_stash') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    character(len=16384) :: output

    output = exec_cmd('git stash 2>&1')
    if (index(output, 'No local changes') > 0) then
        call msg('git-stash: No local changes to save')
    else
        call msg('git-stash: ' // trim(output(1:min(len_trim(output), 60))))
    end if
    ret = 1
end function fortran_git_stash

function fortran_git_stash_pop(f, n) bind(C, name='fortran_git_stash_pop') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret
    character(len=16384) :: output

    output = exec_cmd('git stash pop 2>&1')
    if (index(output, 'No stash entries') > 0) then
        call msg('git-stash-pop: No stash entries found')
    else
        call msg('git-stash-pop: ' // trim(output(1:min(len_trim(output), 60))))
    end if
    ret = 1
end function fortran_git_stash_pop

function fortran_git_goto(f, n) bind(C, name='fortran_git_goto') result(ret)
    use git_commands
    use, intrinsic :: iso_c_binding
    implicit none
    integer(c_int), value :: f, n
    integer(c_int) :: ret

    ! Placeholder - would jump to file at line from git output
    call msg('git-goto: Not yet implemented')
    ret = 1
end function fortran_git_goto
