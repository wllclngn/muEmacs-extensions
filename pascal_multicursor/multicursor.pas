{
  Multiple Cursors Extension for μEmacs
  Written as a Free Pascal Library for shared library creation
}

library multicursor;

{$mode objfpc}{$H+}

uses
  ctypes, SysUtils;

{ C bridge imports }
procedure bridge_message(msg: PChar); cdecl; external;
function bridge_current_buffer: Pointer; cdecl; external;
procedure bridge_get_point(var line, col: cint); cdecl; external;
procedure bridge_set_point(line, col: cint); cdecl; external;
function bridge_buffer_insert(text: PChar; len: csize_t): cint; cdecl; external;

const
  MAX_CURSORS = 64;

type
  TCursor = record
    Line: Integer;
    Col: Integer;
    Active: Boolean;
  end;

var
  Cursors: array[0..MAX_CURSORS-1] of TCursor;
  CursorCount: Integer = 0;
  CurrentCursor: Integer = 0;

{ Helper: Send message }
procedure Msg(const S: string);
begin
  bridge_message(PChar(S));
end;

{ mc-add: Add cursor at current position }
function pascal_mc_add(f, n: cint): cint; cdecl;
var
  Line, Col: cint;
  I: Integer;
begin
  Result := 0;

  if bridge_current_buffer = nil then
  begin
    Msg('mc-add: No buffer');
    Exit;
  end;

  if CursorCount >= MAX_CURSORS then
  begin
    Msg('mc-add: Max cursors reached');
    Exit;
  end;

  bridge_get_point(Line, Col);

  { Check for duplicate }
  for I := 0 to CursorCount - 1 do
  begin
    if (Cursors[I].Line = Line) and (Cursors[I].Col = Col) then
    begin
      Msg('mc-add: Cursor already at ' + IntToStr(Line) + ':' + IntToStr(Col));
      Result := 1;
      Exit;
    end;
  end;

  { Add new cursor }
  Cursors[CursorCount].Line := Line;
  Cursors[CursorCount].Col := Col;
  Cursors[CursorCount].Active := True;
  Inc(CursorCount);

  Msg('mc-add: Cursor ' + IntToStr(CursorCount) + ' at ' + IntToStr(Line) + ':' + IntToStr(Col));
  Result := 1;
end;

{ mc-clear: Clear all cursors }
function pascal_mc_clear(f, n: cint): cint; cdecl;
var
  I: Integer;
begin
  for I := 0 to MAX_CURSORS - 1 do
  begin
    Cursors[I].Line := 0;
    Cursors[I].Col := 0;
    Cursors[I].Active := False;
  end;

  CursorCount := 0;
  CurrentCursor := 0;

  Msg('mc-clear: All cursors cleared');
  Result := 1;
end;

{ mc-next: Jump to next cursor }
function pascal_mc_next(f, n: cint): cint; cdecl;
begin
  Result := 0;

  if CursorCount = 0 then
  begin
    Msg('mc-next: No cursors (use mc-add first)');
    Exit;
  end;

  { Cycle to next cursor }
  CurrentCursor := (CurrentCursor + 1) mod CursorCount;

  { Jump to cursor position }
  bridge_set_point(Cursors[CurrentCursor].Line, Cursors[CurrentCursor].Col);

  Msg('mc-next: Cursor ' + IntToStr(CurrentCursor + 1) + '/' + IntToStr(CursorCount));
  Result := 1;
end;

{ mc-insert: Insert marker at all cursor positions }
function pascal_mc_insert(f, n: cint): cint; cdecl;
var
  I: Integer;
  Line, Col: cint;
  Marker: string;
begin
  Result := 0;

  if CursorCount = 0 then
  begin
    Msg('mc-insert: No cursors');
    Exit;
  end;

  { Save current position }
  bridge_get_point(Line, Col);

  Marker := '|';

  { Insert at each cursor position (reverse order to preserve positions) }
  for I := CursorCount - 1 downto 0 do
  begin
    if Cursors[I].Active then
    begin
      bridge_set_point(Cursors[I].Line, Cursors[I].Col);
      bridge_buffer_insert(PChar(Marker), 1);
    end;
  end;

  { Restore position }
  bridge_set_point(Line, Col);

  Msg('mc-insert: Inserted at ' + IntToStr(CursorCount) + ' positions');
  Result := 1;
end;

{ mc-get-count: Return cursor count for modeline display }
function pascal_mc_get_count: cint; cdecl;
begin
  Result := CursorCount;
end;

{ Export functions }
exports
  pascal_mc_add,
  pascal_mc_clear,
  pascal_mc_next,
  pascal_mc_insert,
  pascal_mc_get_count;

{ Initialize cursor array }
var
  I: Integer;

begin
  for I := 0 to MAX_CURSORS - 1 do
  begin
    Cursors[I].Line := 0;
    Cursors[I].Col := 0;
    Cursors[I].Active := False;
  end;
end.
