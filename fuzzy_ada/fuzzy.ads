-- Fuzzy File Finder Extension for uEmacs
-- Written in Ada 2012

with Interfaces.C;
with Interfaces.C.Strings;
with System;

package Fuzzy is
   use Interfaces.C;
   use Interfaces.C.Strings;

   -- C bridge imports
   procedure Bridge_Message (Msg : chars_ptr)
     with Import, Convention => C, External_Name => "bridge_message";

   function Bridge_Prompt (Prompt : chars_ptr; Buf : chars_ptr; Buflen : size_t) return int
     with Import, Convention => C, External_Name => "bridge_prompt";

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

   -- Fuzzy matching
   function Fuzzy_Match (Pattern, Str : String) return Natural;
   function Fuzzy_Score (Pattern, Str : String) return Natural;

end Fuzzy;
