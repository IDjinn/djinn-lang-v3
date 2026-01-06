; ModuleID = 'build\console'
source_filename = "build\\console"

@0 = private unnamed_addr constant [30 x i8] c"teste output:\0A size=%d\0Ares=%d\00", align 1

declare i32 @puts(ptr)

declare i32 @printf(ptr, ...)

declare ptr @malloc(i64)

declare void @free(ptr)

declare i32 @strlen(ptr)

declare i32 @write(i32, ptr, i32)

define i32 @"std::sys::Console__error"(ptr %message) {
entry:
  %message1 = alloca ptr, align 8
  store ptr %message, ptr %message1, align 8
  %message2 = load ptr, ptr %message1, align 8
  %message3 = load ptr, ptr %message1, align 8
  %0 = call i32 @strlen(ptr %message3)
  %1 = call i32 @write(i32 2, ptr %message2, i32 %0)
  ret i32 %1
}

define i32 @"std::sys::Console__printf"(ptr %message) {
entry:
  %message1 = alloca ptr, align 8
  store ptr %message, ptr %message1, align 8
  %message2 = load ptr, ptr %message1, align 8
  %0 = call i32 (ptr, ...) @printf(ptr %message2)
  ret i32 %0
}

define void @"std::debug::Debug__pause"() {
entry:
  call void @llvm.debugtrap()
  unreachable
}

; Function Attrs: nounwind
declare void @llvm.debugtrap() #0

define void @main() {
entry:
  %size_test = alloca i32, align 4
  store i32 -10, ptr %size_test, align 4
  %res = alloca i32, align 4
  store i32 -1, ptr %res, align 4
  %size_test1 = load i32, ptr %size_test, align 4
  %res2 = load i32, ptr %res, align 4
  %0 = call i32 (ptr, ...) @printf(ptr @0, i32 %size_test1, i32 %res2)
  ret void
}

define i32 @main.1() {
entry:
  ret i32 0
}

attributes #0 = { nounwind }
