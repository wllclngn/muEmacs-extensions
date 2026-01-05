-- Fuzzy File Finder Implementation
--
-- Ada-Friendly Design:
-- - Never allocate buffers for C to fill
-- - Read C's buffers immutably via Value()
-- - Use Bridge_Exec for shell commands (C handles buffering)
-- - Avoid To_Lower(String) - use To_Lower(Character) to prevent secondary stack issues

with Ada.Characters.Handling;

package body Fuzzy is
   use Ada.Characters.Handling;
   use type System.Address;

   -- Helper: Show message to user
   procedure Message (Msg : String) is
      C_Msg : chars_ptr := New_String (Msg);
   begin
      Bridge_Message (C_Msg);
      Free (C_Msg);
   end Message;

   -- Helper: Get string from C bridge (after prompt or exec)
   function Get_Bridge_String return String is
      Len : aliased size_t;
      Ptr : constant chars_ptr := Bridge_Get_String (Len'Access);
   begin
      if Ptr = Null_Ptr or else Len = 0 then
         return "";
      end if;
      return Value (Ptr, Len);
   end Get_Bridge_String;

   -- Fuzzy matching score (higher = better match)
   -- NOTE: Uses character-by-character To_Lower to avoid secondary stack allocation
   function Fuzzy_Score (Pattern, Str : String) return Natural is
      Score       : Natural := 0;
      Pat_Idx     : Natural := Pattern'First;
      Str_Idx     : Natural := Str'First;
      Consecutive : Natural := 0;
      Pat_Lower   : String (Pattern'Range);
      Str_Lower   : String (Str'Range);
   begin
      -- Convert to lowercase character-by-character (avoids secondary stack)
      for I in Pattern'Range loop
         Pat_Lower (I) := To_Lower (Pattern (I));
      end loop;
      for I in Str'Range loop
         Str_Lower (I) := To_Lower (Str (I));
      end loop;

      if Pattern'Length = 0 then
         return 100;
      end if;

      while Pat_Idx <= Pattern'Last and Str_Idx <= Str'Last loop
         if Pat_Lower (Pat_Idx) = Str_Lower (Str_Idx) then
            Score := Score + 10;
            Consecutive := Consecutive + 1;
            Score := Score + Consecutive * 5;

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
         return Score;
      else
         return 0;
      end if;
   end Fuzzy_Score;

   function Fuzzy_Match (Pattern, Str : String) return Natural is
   begin
      if Fuzzy_Score (Pattern, Str) > 0 then
         return 1;
      else
         return 0;
      end if;
   end Fuzzy_Match;

   -- Show results in buffer
   procedure Show_In_Buffer (Buf_Name, Content : String) is
      C_Name : chars_ptr := New_String (Buf_Name);
      C_Text : chars_ptr;
      Bp     : System.Address;
      Dummy  : int;
      I, J   : Natural;
   begin
      Bp := Bridge_Buffer_Create (C_Name);
      Free (C_Name);

      if Bp = System.Null_Address then
         return;
      end if;

      Dummy := Bridge_Buffer_Switch (Bp);
      Dummy := Bridge_Buffer_Clear (Bp);

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
      pragma Unreferenced (F, N);
      C_Prompt   : chars_ptr;
      C_Cmd      : chars_ptr;
      Pattern    : String (1 .. 255);
      Pat_Len    : Natural := 0;
      Files      : String (1 .. 65536);
      Files_Len  : Natural := 0;
      Output     : String (1 .. 65536);
      Out_Len    : Natural := 0;
      I, J       : Natural;
      Score      : Natural;
      Best_Score : Natural := 0;
      Best_File  : String (1 .. 512) := (others => ' ');
      Best_Len   : Natural := 0;
      Count      : Natural := 0;
   begin
      -- Prompt for pattern
      C_Prompt := New_String ("Fuzzy find: ");
      if Bridge_Prompt (C_Prompt) /= 1 then
         Free (C_Prompt);
         Message ("fuzzy-find: Cancelled");
         return 0;
      end if;
      Free (C_Prompt);

      -- Get pattern from C's buffer
      declare
         Val : constant String := Get_Bridge_String;
      begin
         Pat_Len := Natural'Min (Val'Length, Pattern'Length);
         if Pat_Len > 0 then
            Pattern (1 .. Pat_Len) := Val (Val'First .. Val'First + Pat_Len - 1);
         end if;
      end;

      if Pat_Len = 0 then
         Message ("fuzzy-find: Empty pattern");
         return 0;
      end if;

      -- Execute find command via C bridge
      C_Cmd := New_String ("find . -type f \( -name '*.c' -o -name '*.h' -o -name '*.py' -o -name '*.rs' -o -name '*.go' -o -name '*.zig' -o -name '*.ada' -o -name '*.adb' -o -name '*.ads' \) 2>/dev/null | head -500");
      if Bridge_Exec (C_Cmd) /= 1 then
         Free (C_Cmd);
         Message ("fuzzy-find: Failed to list files");
         return 0;
      end if;
      Free (C_Cmd);

      -- Get file list from C's buffer
      declare
         Val : constant String := Get_Bridge_String;
         Copy_Len : constant Natural := Natural'Min (Val'Length, Files'Length);
      begin
         if Copy_Len > 0 then
            Files (1 .. Copy_Len) := Val (Val'First .. Val'First + Copy_Len - 1);
         end if;
         Files_Len := Copy_Len;
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

                  if Out_Len + File_Path'Length + 1 < Output'Last then
                     Output (Out_Len + 1 .. Out_Len + File_Path'Length) := File_Path;
                     Out_Len := Out_Len + File_Path'Length;
                     Output (Out_Len + 1) := ASCII.LF;
                     Out_Len := Out_Len + 1;
                  end if;

                  if Score > Best_Score and then File_Path'Length <= Best_File'Length then
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

      if Count = 1 and then Best_Len > 0 then
         declare
            C_Path : chars_ptr := New_String (Best_File (1 .. Best_Len));
            Dummy  : int;
         begin
            Dummy := Bridge_Find_File_Line (C_Path, 1);
            Free (C_Path);
            Message ("fuzzy-find: " & Best_File (1 .. Best_Len));
         end;
      else
         Show_In_Buffer ("*fuzzy-find*", Output (1 .. Out_Len));
         Message ("fuzzy-find: " & Natural'Image (Count) & " matches (Enter to open)");
      end if;

      return 1;
   end Ada_Fuzzy_Find;

   -- fuzzy-grep command
   function Ada_Fuzzy_Grep (F, N : int) return int is
      pragma Unreferenced (F, N);
      C_Prompt : chars_ptr;
      C_Cmd    : chars_ptr;
      Pattern  : String (1 .. 255);
      Pat_Len  : Natural := 0;
   begin
      -- Prompt for pattern
      C_Prompt := New_String ("Fuzzy grep: ");
      if Bridge_Prompt (C_Prompt) /= 1 then
         Free (C_Prompt);
         Message ("fuzzy-grep: Cancelled");
         return 0;
      end if;
      Free (C_Prompt);

      -- Get pattern from C's buffer
      declare
         Val : constant String := Get_Bridge_String;
      begin
         Pat_Len := Natural'Min (Val'Length, Pattern'Length);
         if Pat_Len > 0 then
            Pattern (1 .. Pat_Len) := Val (Val'First .. Val'First + Pat_Len - 1);
         end if;
      end;

      if Pat_Len = 0 then
         Message ("fuzzy-grep: Empty pattern");
         return 0;
      end if;

      -- Build and execute grep command
      declare
         Pat : constant String := Pattern (1 .. Pat_Len);
         Cmd : constant String := "rg -n --no-heading '" & Pat &
                   "' 2>/dev/null || grep -rn '" & Pat &
                   "' . 2>/dev/null | head -100";
      begin
         C_Cmd := New_String (Cmd);
         if Bridge_Exec (C_Cmd) /= 1 then
            Free (C_Cmd);
            Message ("fuzzy-grep: No matches");
            return 1;
         end if;
         Free (C_Cmd);

         declare
            Raw : constant String := Get_Bridge_String;
         begin
            if Raw'Length < 2 then
               Message ("fuzzy-grep: No matches");
               return 1;
            end if;

            Show_In_Buffer ("*fuzzy-grep*", Raw);
            Message ("fuzzy-grep: Results shown");
         end;
      end;

      return 1;
   end Ada_Fuzzy_Grep;

end Fuzzy;
