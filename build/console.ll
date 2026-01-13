; ModuleID = 'build\console'
source_filename = "build\\console"

declare ptr @fopen(ptr, ptr)

declare i32 @fclose(ptr)

declare i32 @fflush(ptr)

declare ptr @freopen(ptr, ptr, ptr)

declare i32 @fseek(ptr, i64, i32)

declare i64 @ftell(ptr)

declare void @rewind(ptr)

declare i32 @fgetpos(ptr, ptr)

declare i32 @fsetpos(ptr, ptr)

declare i64 @fread(ptr, i64, i64, ptr)

declare i64 @fwrite(ptr, i64, i64, ptr)

declare i32 @fgetc(ptr)

declare i32 @fputc(i32, ptr)

declare i32 @getc(ptr)

declare i32 @putc(i32, ptr)

declare i32 @getchar()

declare i32 @putchar(i32)

declare i32 @ungetc(i32, ptr)

declare ptr @fgets(ptr, i32, ptr)

declare i32 @fputs(ptr, ptr)

declare i32 @puts(ptr)

declare ptr @gets(ptr)

declare i32 @fprintf(ptr, ptr, ...)

declare i32 @fscanf(ptr, ptr, ...)

declare i32 @sprintf(ptr, ptr, ...)

declare i32 @snprintf(ptr, i64, ptr, ...)

declare i32 @sscanf(ptr, ptr, ...)

declare i32 @feof(ptr)

declare i32 @ferror(ptr)

declare void @clearerr(ptr)

declare void @perror(ptr)

declare i32 @remove(ptr)

declare i32 @rename(ptr, ptr)

declare ptr @tmpfile()

declare ptr @tmpnam(ptr)

declare i32 @open(ptr, i32, ...)

declare i32 @close(i32)

declare i64 @read(i32, ptr, i64)

declare i64 @write(i32, ptr, i64)

declare i64 @lseek(i32, i64, i32)

declare i32 @fileno(ptr)

declare ptr @fdopen(i32, ptr)

declare i32 @dup(i32)

declare i32 @dup2(i32, i32)

declare i32 @pipe(ptr)

declare ptr @popen(ptr, ptr)

declare i32 @pclose(ptr)

declare i32 @printf(ptr, ...)

declare ptr @malloc(i64)

declare void @free(ptr)

declare i32 @strlen(ptr)

declare double @sin(double)

declare double @cos(double)

declare double @tan(double)

declare double @asin(double)

declare double @acos(double)

declare double @atan(double)

declare double @atan2(double, double)

declare double @sinh(double)

declare double @cosh(double)

declare double @tanh(double)

declare double @asinh(double)

declare double @acosh(double)

declare double @atanh(double)

declare double @exp(double)

declare double @exp2(double)

declare double @expm1(double)

declare double @log(double)

declare double @log10(double)

declare double @log2(double)

declare double @log1p(double)

declare double @pow(double, double)

declare double @sqrt(double)

declare double @cbrt(double)

declare double @hypot(double, double)

declare double @ceil(double)

declare double @floor(double)

declare double @trunc(double)

declare double @round(double)

declare double @fmod(double, double)

declare double @remainder(double, double)

declare double @fabs(double)

declare double @fmax(double, double)

declare double @fmin(double, double)

declare double @erf(double)

declare double @erfc(double)

declare double @tgamma(double)

declare double @lgamma(double)

declare float @sinf(float)

declare float @cosf(float)

declare float @tanf(float)

declare float @asinf(float)

declare float @acosf(float)

declare float @atanf(float)

declare float @atan2f(float, float)

declare float @sinhf(float)

declare float @coshf(float)

declare float @tanhf(float)

declare float @expf(float)

declare float @logf(float)

declare float @log10f(float)

declare float @powf(float, float)

declare float @sqrtf(float)

declare float @cbrtf(float)

declare float @ceilf(float)

declare float @floorf(float)

declare float @truncf(float)

declare float @roundf(float)

declare float @fmodf(float, float)

declare float @fabsf(float)

declare float @fmaxf(float, float)

declare float @fminf(float, float)

define i32 @Console__error(ptr %message) {
entry:
  %message1 = alloca ptr, align 8
  store ptr %message, ptr %message1, align 8
  %message2 = load ptr, ptr %message1, align 8
  %message3 = load ptr, ptr %message1, align 8
  %0 = call i32 @strlen(ptr %message3)
  %sext = sext i32 %0 to i64
  %1 = call i64 @write(i32 2, ptr %message2, i64 %sext)
  %trunc = trunc i64 %1 to i32
  ret i32 %trunc
}

define i32 @Console__printf(ptr %message) {
entry:
  %message1 = alloca ptr, align 8
  store ptr %message, ptr %message1, align 8
  %message2 = load ptr, ptr %message1, align 8
  %0 = call i32 (ptr, ...) @printf(ptr %message2)
  ret i32 %0
}

define void @Debug__pause() {
entry:
  call void @llvm.debugtrap()
  unreachable
}

; Function Attrs: nounwind
declare void @llvm.debugtrap() #0

define void @main() {
entry:
  ret void
}

define i32 @main.1() {
entry:
  ret i32 0
}

attributes #0 = { nounwind }
