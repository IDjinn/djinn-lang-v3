; ModuleID = 'build\console'
source_filename = "build\\console"

@0 = private unnamed_addr constant [30 x i8] c"teste output:\0A size=%d\0Ares=%d\00", align 1

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr noundef readonly captures(none), ...) local_unnamed_addr #0

declare i32 @strlen(ptr) local_unnamed_addr

declare i32 @write(i32, ptr, i32) local_unnamed_addr

define i32 @"std::sys::Console__error"(ptr %message) local_unnamed_addr {
entry:
  %0 = tail call i32 @strlen(ptr %message)
  %1 = tail call i32 @write(i32 2, ptr %message, i32 %0)
  ret i32 %1
}

; Function Attrs: nofree nounwind
define noundef i32 @"std::sys::Console__printf"(ptr readonly captures(none) %message) local_unnamed_addr #0 {
entry:
  %0 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) %message)
  ret i32 %0
}

; Function Attrs: noreturn nounwind
define void @"std::debug::Debug__pause"() local_unnamed_addr #1 {
entry:
  tail call void @llvm.debugtrap()
  unreachable
}

; Function Attrs: nounwind
declare void @llvm.debugtrap() #2

; Function Attrs: nofree nounwind
define void @main() local_unnamed_addr #0 {
entry:
  %0 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @0, i32 -10, i32 -1)
  ret void
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef i32 @main.1() local_unnamed_addr #3 {
entry:
  ret i32 0
}

attributes #0 = { nofree nounwind }
attributes #1 = { noreturn nounwind }
attributes #2 = { nounwind }
attributes #3 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) }
