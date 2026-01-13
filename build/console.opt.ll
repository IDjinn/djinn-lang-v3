; ModuleID = 'build\console'
source_filename = "build\\console"

; Function Attrs: nofree
declare noundef i64 @write(i32 noundef, ptr noundef readonly captures(none), i64 noundef) local_unnamed_addr #0

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr noundef readonly captures(none), ...) local_unnamed_addr #1

declare i32 @strlen(ptr) local_unnamed_addr

define noundef i32 @Console__error(ptr %message) local_unnamed_addr {
entry:
  %0 = tail call i32 @strlen(ptr %message)
  %sext = sext i32 %0 to i64
  %1 = tail call i64 @write(i32 2, ptr %message, i64 %sext)
  %trunc = trunc i64 %1 to i32
  ret i32 %trunc
}

; Function Attrs: nofree nounwind
define noundef i32 @Console__printf(ptr readonly captures(none) %message) local_unnamed_addr #1 {
entry:
  %0 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) %message)
  ret i32 %0
}

; Function Attrs: noreturn nounwind
define void @Debug__pause() local_unnamed_addr #2 {
entry:
  tail call void @llvm.debugtrap()
  unreachable
}

; Function Attrs: nounwind
declare void @llvm.debugtrap() #3

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define void @main() local_unnamed_addr #4 {
entry:
  ret void
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef i32 @main.1() local_unnamed_addr #4 {
entry:
  ret i32 0
}

attributes #0 = { nofree }
attributes #1 = { nofree nounwind }
attributes #2 = { noreturn nounwind }
attributes #3 = { nounwind }
attributes #4 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) }
