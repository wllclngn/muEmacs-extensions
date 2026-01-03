-- Fuzzy File Finder Implementation

with Ada.Characters.Handling;
with Ada.Strings.Fixed;
with Ada.Text_IO;
with Ada.Directories;
with GNAT.OS_Lib;
with System.Address_To_Access_Conversions;

package body Fuzzy is
   use Ada.Characters.Handling;
   use Ada.Strings.Fixed;
   use type System.Address;

   -- Message helper
   procedure Message (Msg : String) is
      C_Msg : chars_ptr := New_String (Msg);
   begin
      Bridge_Message (C_Msg);
      Free (C_Msg);
   end Message;

   -- Fuzzy matching score (higher = better match)
   function Fuzzy_Score (Pattern, Str : String) return Natural is
      Score      : Natural := 0;
      Pat_Idx    : Natural := Pattern'First;
      Str_Idx    : Natural := Str'First;
      Consecutive : Natural := 0;
      Pat_Lower  : constant String := To_Lower (Pattern);
      Str_Lower  : constant String := To_Lower (Str);
   begin
      if Pattern'Length = 0 then
         return 100;
      end if;

      while Pat_Idx <= Pattern'Last and Str_Idx <= Str'Last loop
         if Pat_Lower (Pat_Idx) = Str_Lower (Str_Idx) then
            Score := Score + 10;  -- Base match
            Consecutive := Consecutive + 1;
            Score := Score + Consecutive * 5;  -- Consecutive bonus

            -- Bonus for matching at word boundaries
            if Str_Idx = Str'First or else
               Str (Str_Idx - 1) = '/' or else
               Str (Str_Idx - 1) = '_' or else
               Str (Str_Idx - 1) = '.'
            then
               Score := Score + 20;
            end if;

            Pat_Idx := Pat_Idx + 1;
         else
            Consecutive := 0;
         end if;
         Str_Idx := Str_Idx + 1;
      end loop;

      if Pat_Idx > Pattern'Last then
         return Score;  -- Full pattern matched
      else
         return 0;  -- Pattern not fully matched
      end if;
   end Fuzzy_Score;

   -- Simple match check (returns 1 if matches, 0 otherwise)
   function Fuzzy_Match (Pattern, Str : String) return Natural is
   begin
      if Fuzzy_Score (Pattern, Str) > 0 then
         return 1;
      else
         return 0;
      end if;
   end Fuzzy_Match;

   -- Execute command and get output
   function Exec_Cmd (Cmd : String) return String is
      Tmp_File : constant String := "/tmp/ada_fuzzy_out.tmp";
      Full_Cmd : constant String := Cmd & " > " & Tmp_File & " 2>/dev/null";
      Result   : String (1 .. 32768) := (others => ' ');
      File     : Ada.Text_IO.File_Type;
      Last     : Natural := 0;
      Line     : String (1 .. 1024);
      Len      : Natural;
      Args     : GNAT.OS_Lib.Argument_List_Access;
      Success  : Boolean;
   begin
      -- Use system() via shell
      Args := GNAT.OS_Lib.Argument_String_To_List ("/bin/sh -c """ & Full_Cmd & """");
      GNAT.OS_Lib.Spawn ("/bin/sh", Args.all, Success);
      GNAT.OS_Lib.Free (Args);

      begin
         Ada.Text_IO.Open (File, Ada.Text_IO.In_File, Tmp_File);
         while not Ada.Text_IO.End_Of_File (File) loop
            Ada.Text_IO.Get_Line (File, Line, Len);
            if Last + Len + 1 < Result'Last then
               Result (Last + 1 .. Last + Len) := Line (1 .. Len);
               Last := Last + Len;
               Result (Last + 1) := ASCII.LF;
               Last := Last + 1;
            end if;
         end loop;
         Ada.Text_IO.Close (File);
         Ada.Directories.Delete_File (Tmp_File);
      exception
         when others => null;
      end;

      return Result (1 .. Last);
   end Exec_Cmd;

   -- Show results in buffer
   procedure Show_In_Buffer (Buf_Name, Content : String) is
      C_Name  : chars_ptr := New_String (Buf_Name);
      C_Text  : chars_ptr;
      Bp      : System.Address;
      Dummy   : int;
      I, J    : Natural;
   begin
      Bp := Bridge_Buffer_Create (C_Name);
      Free (C_Name);

      if Bp = System.Null_Address then
         return;
      end if;

      Dummy := Bridge_Buffer_Switch (Bp);
      Dummy := Bridge_Buffer_Clear (Bp);

      -- Insert line by line
      I := Content'First;
      while I <= Content'Last loop
         J := I;
         while J <= Content'Last and then Content (J) /= ASCII.LF loop
            J := J + 1;
         end loop;

         if J > I then
            declare
               Line : constant String := Content (I .. J - 1) & ASCII.LF;
            begin
               C_Text := New_String (Line);
               Dummy := Bridge_Buffer_Insert (C_Text, size_t (Line'Length));
               Free (C_Text);
            end;
         end if;

         I := J + 1;
      end loop;
   end Show_In_Buffer;

   -- fuzzy-find command
   function Ada_Fuzzy_Find (F, N : int) return int is
      Pattern_Buf : char_array (0 .. 255) := (others => nul);
      C_Prompt    : chars_ptr := New_String ("Fuzzy find: ");
      C_Pattern   : chars_ptr;
      Result      : int;
      Pattern     : String (1 .. 255);
      Pat_Len     : Natural := 0;
      Files       : String (1 .. 32768);
      Files_Len   : Natural := 0;
      Output      : String (1 .. 32768);
      Out_Len     : Natural := 0;
      I, J        : Natural;
      Score       : Natural;
      Best_Score  : Natural := 0;
      Best_File   : String (1 .. 512) := (others => ' ');
      Best_Len    : Natural := 0;
      Count       : Natural := 0;
   begin
      -- Prompt for pattern
      C_Pattern := New_Char_Array (Pattern_Buf);
      Result := Bridge_Prompt (C_Prompt, C_Pattern, 256);
      Free (C_Prompt);

      if Result = 0 then
         Free (C_Pattern);
         Message ("fuzzy-find: Cancelled");
         return 0;
      end if;

      -- Convert to Ada string
      declare
         Val : constant String := Value (C_Pattern);
      begin
         Pat_Len := Val'Length;
         if Pat_Len > 0 then
            Pattern (1 .. Pat_Len) := Val;
         end if;
      end;
      Free (C_Pattern);

      if Pat_Len = 0 then
         Message ("fuzzy-find: Empty pattern");
         return 0;
      end if;

      -- Get file list (using find)
      declare
         Raw : constant String := Exec_Cmd ("find . -type f -name '*.c' -o -name '*.h' -o -name '*.py' -o -name '*.rs' -o -name '*.go' -o -name '*.zig' 2>/dev/null | head -500");
      begin
         Files (1 .. Raw'Length) := Raw;
         Files_Len := Raw'Length;
      end;

      -- Fuzzy match each file
      I := 1;
      while I <= Files_Len loop
         J := I;
         while J <= Files_Len and then Files (J) /= ASCII.LF loop
            J := J + 1;
         end loop;

         if J > I then
            declare
               File_Path : constant String := Files (I .. J - 1);
            begin
               Score := Fuzzy_Score (Pattern (1 .. Pat_Len), File_Path);
               if Score > 0 then
                  Count := Count + 1;

                  -- Add to output
                  if Out_Len + File_Path'Length + 1 < Output'Last then
                     Output (Out_Len + 1 .. Out_Len + File_Path'Length) := File_Path;
                     Out_Len := Out_Len + File_Path'Length;
                     Output (Out_Len + 1) := ASCII.LF;
                     Out_Len := Out_Len + 1;
                  end if;

                  -- Track best match
                  if Score > Best_Score then
                     Best_Score := Score;
                     Best_File (1 .. File_Path'Length) := File_Path;
                     Best_Len := File_Path'Length;
                  end if;
               end if;
            end;
         end if;

         I := J + 1;
      end loop;

      if Count = 0 then
         Message ("fuzzy-find: No matches");
         return 1;
      end if;

      -- If only one match or user wants immediate, open best match
      if Count = 1 or else Best_Len > 0 then
         declare
            C_Path : chars_ptr := New_String (Best_File (1 .. Best_Len));
            Dummy  : int;
         begin
            Dummy := Bridge_Find_File_Line (C_Path, 1);
            Free (C_Path);
            Message ("fuzzy-find: " & Best_File (1 .. Best_Len));
         end;
      else
         -- Show all matches in buffer
         Show_In_Buffer ("*fuzzy-find*", Output (1 .. Out_Len));
         Message ("fuzzy-find: " & Natural'Image (Count) & " matches");
      end if;

      return 1;
   end Ada_Fuzzy_Find;

   -- fuzzy-grep command
   function Ada_Fuzzy_Grep (F, N : int) return int is
      Pattern_Buf : char_array (0 .. 255) := (others => nul);
      C_Prompt    : chars_ptr := New_String ("Fuzzy grep: ");
      C_Pattern   : chars_ptr;
      Result      : int;
      Pattern     : String (1 .. 255);
      Pat_Len     : Natural := 0;
      Output      : String (1 .. 32768);
      Out_Len     : Natural := 0;
      Cmd         : String (1 .. 512);
      Cmd_Len     : Natural;
   begin
      -- Prompt for pattern
      C_Pattern := New_Char_Array (Pattern_Buf);
      Result := Bridge_Prompt (C_Prompt, C_Pattern, 256);
      Free (C_Prompt);

      if Result = 0 then
         Free (C_Pattern);
         Message ("fuzzy-grep: Cancelled");
         return 0;
      end if;

      -- Convert to Ada string
      declare
         Val : constant String := Value (C_Pattern);
      begin
         Pat_Len := Val'Length;
         if Pat_Len > 0 then
            Pattern (1 .. Pat_Len) := Val;
         end if;
      end;
      Free (C_Pattern);

      if Pat_Len = 0 then
         Message ("fuzzy-grep: Empty pattern");
         return 0;
      end if;

      -- Build grep command (use rg if available, else grep)
      Cmd := "rg -n --no-heading '" & Pattern (1 .. Pat_Len) & "' 2>/dev/null || grep -rn '" & Pattern (1 .. Pat_Len) & "' . 2>/dev/null | head -100" & (Cmd'Last - 100 - Pat_Len * 2 => ' ');
      Cmd_Len := 60 + Pat_Len * 2;

      declare
         Raw : constant String := Exec_Cmd (Cmd (1 .. Cmd_Len));
      begin
         if Raw'Length < 2 then
            Message ("fuzzy-grep: No matches");
            return 1;
         end if;

         Show_In_Buffer ("*fuzzy-grep*", Raw);
         Message ("fuzzy-grep: Results shown");
      end;

      return 1;
   end Ada_Fuzzy_Grep;

end Fuzzy;
