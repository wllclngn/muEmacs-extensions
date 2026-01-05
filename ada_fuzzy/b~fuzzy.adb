pragma Warnings (Off);
pragma Ada_95;
pragma Source_File_Name (ada_main, Spec_File_Name => "b~fuzzy.ads");
pragma Source_File_Name (ada_main, Body_File_Name => "b~fuzzy.adb");
pragma Suppress (Overflow_Check);

package body ada_main is

   E065 : Short_Integer; pragma Import (Ada, E065, "system__os_lib_E");
   E019 : Short_Integer; pragma Import (Ada, E019, "ada__exceptions_E");
   E015 : Short_Integer; pragma Import (Ada, E015, "system__soft_links_E");
   E013 : Short_Integer; pragma Import (Ada, E013, "system__exception_table_E");
   E038 : Short_Integer; pragma Import (Ada, E038, "ada__containers_E");
   E060 : Short_Integer; pragma Import (Ada, E060, "ada__io_exceptions_E");
   E028 : Short_Integer; pragma Import (Ada, E028, "ada__numerics_E");
   E010 : Short_Integer; pragma Import (Ada, E010, "ada__strings_E");
   E099 : Short_Integer; pragma Import (Ada, E099, "ada__strings__maps_E");
   E102 : Short_Integer; pragma Import (Ada, E102, "ada__strings__maps__constants_E");
   E043 : Short_Integer; pragma Import (Ada, E043, "interfaces__c_E");
   E022 : Short_Integer; pragma Import (Ada, E022, "system__exceptions_E");
   E076 : Short_Integer; pragma Import (Ada, E076, "system__object_reader_E");
   E050 : Short_Integer; pragma Import (Ada, E050, "system__dwarf_lines_E");
   E095 : Short_Integer; pragma Import (Ada, E095, "system__soft_links__initialize_E");
   E037 : Short_Integer; pragma Import (Ada, E037, "system__traceback__symbolic_E");
   E104 : Short_Integer; pragma Import (Ada, E104, "interfaces__c__strings_E");
   E002 : Short_Integer; pragma Import (Ada, E002, "fuzzy_E");

   Sec_Default_Sized_Stacks : array (1 .. 1) of aliased System.Secondary_Stack.SS_Stack (System.Parameters.Runtime_Default_Sec_Stack_Size);

   Local_Priority_Specific_Dispatching : constant String := "";
   Local_Interrupt_States : constant String := "";

   Is_Elaborated : Boolean := False;

   procedure adafinal is
      procedure s_stalib_adafinal;
      pragma Import (Ada, s_stalib_adafinal, "system__standard_library__adafinal");

      procedure Runtime_Finalize;
      pragma Import (C, Runtime_Finalize, "__gnat_runtime_finalize");

   begin
      if not Is_Elaborated then
         return;
      end if;
      Is_Elaborated := False;
      Runtime_Finalize;
      s_stalib_adafinal;
   end adafinal;

   type No_Param_Proc is access procedure;
   pragma Favor_Top_Level (No_Param_Proc);

   procedure adainit is
      Main_Priority : Integer;
      pragma Import (C, Main_Priority, "__gl_main_priority");
      Time_Slice_Value : Integer;
      pragma Import (C, Time_Slice_Value, "__gl_time_slice_val");
      WC_Encoding : Character;
      pragma Import (C, WC_Encoding, "__gl_wc_encoding");
      Locking_Policy : Character;
      pragma Import (C, Locking_Policy, "__gl_locking_policy");
      Queuing_Policy : Character;
      pragma Import (C, Queuing_Policy, "__gl_queuing_policy");
      Task_Dispatching_Policy : Character;
      pragma Import (C, Task_Dispatching_Policy, "__gl_task_dispatching_policy");
      Priority_Specific_Dispatching : System.Address;
      pragma Import (C, Priority_Specific_Dispatching, "__gl_priority_specific_dispatching");
      Num_Specific_Dispatching : Integer;
      pragma Import (C, Num_Specific_Dispatching, "__gl_num_specific_dispatching");
      Main_CPU : Integer;
      pragma Import (C, Main_CPU, "__gl_main_cpu");
      Interrupt_States : System.Address;
      pragma Import (C, Interrupt_States, "__gl_interrupt_states");
      Num_Interrupt_States : Integer;
      pragma Import (C, Num_Interrupt_States, "__gl_num_interrupt_states");
      Unreserve_All_Interrupts : Integer;
      pragma Import (C, Unreserve_All_Interrupts, "__gl_unreserve_all_interrupts");
      Detect_Blocking : Integer;
      pragma Import (C, Detect_Blocking, "__gl_detect_blocking");
      Default_Stack_Size : Integer;
      pragma Import (C, Default_Stack_Size, "__gl_default_stack_size");
      Default_Secondary_Stack_Size : System.Parameters.Size_Type;
      pragma Import (C, Default_Secondary_Stack_Size, "__gnat_default_ss_size");
      Bind_Env_Addr : System.Address;
      pragma Import (C, Bind_Env_Addr, "__gl_bind_env_addr");
      Interrupts_Default_To_System : Integer;
      pragma Import (C, Interrupts_Default_To_System, "__gl_interrupts_default_to_system");

      procedure Runtime_Initialize (Install_Handler : Integer);
      pragma Import (C, Runtime_Initialize, "__gnat_runtime_initialize");

      Finalize_Library_Objects : No_Param_Proc;
      pragma Import (C, Finalize_Library_Objects, "__gnat_finalize_library_objects");
      Binder_Sec_Stacks_Count : Natural;
      pragma Import (Ada, Binder_Sec_Stacks_Count, "__gnat_binder_ss_count");
      Default_Sized_SS_Pool : System.Address;
      pragma Import (Ada, Default_Sized_SS_Pool, "__gnat_default_ss_pool");

   begin
      if Is_Elaborated then
         return;
      end if;
      Is_Elaborated := True;
      Main_Priority := -1;
      Time_Slice_Value := -1;
      WC_Encoding := 'b';
      Locking_Policy := ' ';
      Queuing_Policy := ' ';
      Task_Dispatching_Policy := ' ';
      Priority_Specific_Dispatching :=
        Local_Priority_Specific_Dispatching'Address;
      Num_Specific_Dispatching := 0;
      Main_CPU := -1;
      Interrupt_States := Local_Interrupt_States'Address;
      Num_Interrupt_States := 0;
      Unreserve_All_Interrupts := 0;
      Detect_Blocking := 0;
      Default_Stack_Size := -1;

      ada_main'Elab_Body;
      Default_Secondary_Stack_Size := System.Parameters.Runtime_Default_Sec_Stack_Size;
      Binder_Sec_Stacks_Count := 1;
      Default_Sized_SS_Pool := Sec_Default_Sized_Stacks'Address;

      Runtime_Initialize (1);

      Finalize_Library_Objects := null;

      if E019 = 0 then
         Ada.Exceptions'Elab_Spec;
      end if;
      if E015 = 0 then
         System.Soft_Links'Elab_Spec;
      end if;
      if E013 = 0 then
         System.Exception_Table'Elab_Body;
      end if;
      E013 := E013 + 1;
      if E038 = 0 then
         Ada.Containers'Elab_Spec;
      end if;
      E038 := E038 + 1;
      if E060 = 0 then
         Ada.Io_Exceptions'Elab_Spec;
      end if;
      E060 := E060 + 1;
      if E028 = 0 then
         Ada.Numerics'Elab_Spec;
      end if;
      E028 := E028 + 1;
      if E010 = 0 then
         Ada.Strings'Elab_Spec;
      end if;
      E010 := E010 + 1;
      if E099 = 0 then
         Ada.Strings.Maps'Elab_Spec;
      end if;
      E099 := E099 + 1;
      if E102 = 0 then
         Ada.Strings.Maps.Constants'Elab_Spec;
      end if;
      E102 := E102 + 1;
      if E043 = 0 then
         Interfaces.C'Elab_Spec;
      end if;
      E043 := E043 + 1;
      if E022 = 0 then
         System.Exceptions'Elab_Spec;
      end if;
      E022 := E022 + 1;
      if E076 = 0 then
         System.Object_Reader'Elab_Spec;
      end if;
      E076 := E076 + 1;
      if E050 = 0 then
         System.Dwarf_Lines'Elab_Spec;
      end if;
      E050 := E050 + 1;
      if E065 = 0 then
         System.Os_Lib'Elab_Body;
      end if;
      E065 := E065 + 1;
      if E095 = 0 then
         System.Soft_Links.Initialize'Elab_Body;
      end if;
      E095 := E095 + 1;
      E015 := E015 + 1;
      if E037 = 0 then
         System.Traceback.Symbolic'Elab_Body;
      end if;
      E037 := E037 + 1;
      E019 := E019 + 1;
      if E104 = 0 then
         Interfaces.C.Strings'Elab_Spec;
      end if;
      E104 := E104 + 1;
      E002 := E002 + 1;
   end adainit;

--  BEGIN Object file/option list
   --   ./fuzzy.o
   --   -L./
   --   -L/usr/lib/gcc/x86_64-pc-linux-gnu/15.2.1/adalib/
   --   -shared
   --   -lgnat-15
   --   -ldl
--  END Object file/option list   

end ada_main;
