-- Fuzzy File Finder Extension for uEmacs
-- Written in Ada 2012
--
-- Ada-Friendly Bridge Design:
-- - C owns all mutable buffers
-- - Ada reads immutably via Value()
-- - Status and data are separate

with Interfaces.C;
with Interfaces.C.Strings;
with System;

package Fuzzy is
   use Interfaces.C;
   use Interfaces.C.Strings;

   -- Bridge: Simple operations (no buffer exchange)
   procedure Bridge_Message (Msg : chars_ptr)
     with Import, Convention => C, External_Name => "bridge_message";

   -- Bridge: Prompt user - stores result in C's internal buffer
   -- Returns 1 on success, 0 on cancel
   function Bridge_Prompt (Prompt : chars_ptr) return int
     with Import, Convention => C, External_Name => "bridge_prompt";

   -- Bridge: Execute shell command - stores output in C's internal buffer
   -- Returns 1 on success, 0 on failure
   function Bridge_Exec (Cmd : chars_ptr) return int
     with Import, Convention => C, External_Name => "bridge_exec";

   -- Bridge: Get last string result (prompt input or command output)
   -- Returns pointer to C's internal buffer (read-only!)
   function Bridge_Get_String (Out_Len : access size_t) return chars_ptr
     with Import, Convention => C, External_Name => "bridge_get_string";

   -- Bridge: Get string length only
   function Bridge_Get_String_Length return size_t
     with Import, Convention => C, External_Name => "bridge_get_string_length";

   -- Bridge: Buffer operations (pass-through)
   function Bridge_Buffer_Create (Name : chars_ptr) return System.Address
     with Import, Convention => C, External_Name => "bridge_buffer_create";

   function Bridge_Buffer_Switch (Bp : System.Address) return int
     with Import, Convention => C, External_Name => "bridge_buffer_switch";

   function Bridge_Buffer_Clear (Bp : System.Address) return int
     with Import, Convention => C, External_Name => "bridge_buffer_clear";

   function Bridge_Buffer_Insert (Text : chars_ptr; Len : size_t) return int
     with Import, Convention => C, External_Name => "bridge_buffer_insert";

   function Bridge_Find_File_Line (Path : chars_ptr; Line : int) return int
     with Import, Convention => C, External_Name => "bridge_find_file_line";

   -- Command exports for C
   function Ada_Fuzzy_Find (F, N : int) return int
     with Export, Convention => C, External_Name => "ada_fuzzy_find";

   function Ada_Fuzzy_Grep (F, N : int) return int
     with Export, Convention => C, External_Name => "ada_fuzzy_grep";

   -- Fuzzy matching (pure Ada)
   function Fuzzy_Match (Pattern, Str : String) return Natural;
   function Fuzzy_Score (Pattern, Str : String) return Natural;

end Fuzzy;
