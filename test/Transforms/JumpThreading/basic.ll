; RUN: opt %s -jump-threading -S -enable-jump-threading-lvi | FileCheck %s

declare i32 @f1()
declare i32 @f2()
declare void @f3()

define i32 @test1(i1 %cond) {
; CHECK: @test1

	br i1 %cond, label %T1, label %F1

T1:
	%v1 = call i32 @f1()
	br label %Merge

F1:
	%v2 = call i32 @f2()
	br label %Merge

Merge:
	%A = phi i1 [true, %T1], [false, %F1]
	%B = phi i32 [%v1, %T1], [%v2, %F1]
	br i1 %A, label %T2, label %F2

T2:
; CHECK: T2:
; CHECK: ret i32 %v1
	call void @f3()
	ret i32 %B

F2:
; CHECK: F2:
; CHECK: ret i32 %v2
	ret i32 %B
}


;; cond is known false on Entry -> F1 edge!
define i32 @test2(i1 %cond) {
; CHECK: @test2
Entry:
	br i1 %cond, label %T1, label %F1

T1:
; CHECK: %v1 = call i32 @f1()
; CHECK: ret i32 47
	%v1 = call i32 @f1()
	br label %Merge

F1:
	br i1 %cond, label %Merge, label %F2

Merge:
	%B = phi i32 [47, %T1], [192, %F1]
	ret i32 %B

F2:
	call void @f3()
	ret i32 12
}


; Undef handling.
define i32 @test3(i1 %cond) {
; CHECK: @test3
; CHECK-NEXT: T1:
; CHECK-NEXT: ret i32 42
	br i1 undef, label %T1, label %F1

T1:
	ret i32 42

F1:
	ret i32 17
}

define i32 @test4(i1 %cond, i1 %cond2) {
; CHECK: @test4

	br i1 %cond, label %T1, label %F1

T1:
; CHECK:   %v1 = call i32 @f1()
; CHECK-NEXT:   br label %T

	%v1 = call i32 @f1()
	br label %Merge

F1:
	%v2 = call i32 @f2()
; CHECK:   %v2 = call i32 @f2()
; CHECK-NEXT:   br i1 %cond2, 
	br label %Merge

Merge:
	%A = phi i1 [undef, %T1], [%cond2, %F1]
	%B = phi i32 [%v1, %T1], [%v2, %F1]
	br i1 %A, label %T2, label %F2

T2:
	call void @f3()
	ret i32 %B

F2:
	ret i32 %B
}


;; This tests that the branch in 'merge' can be cloned up into T1.
define i32 @test5(i1 %cond, i1 %cond2) {
; CHECK: @test5

	br i1 %cond, label %T1, label %F1

T1:
; CHECK: T1:
; CHECK-NEXT:   %v1 = call i32 @f1()
; CHECK-NEXT:   %cond3 = icmp eq i32 %v1, 412
; CHECK-NEXT:   br i1 %cond3, label %T2, label %F2

	%v1 = call i32 @f1()
        %cond3 = icmp eq i32 %v1, 412
	br label %Merge

F1:
	%v2 = call i32 @f2()
	br label %Merge

Merge:
	%A = phi i1 [%cond3, %T1], [%cond2, %F1]
	%B = phi i32 [%v1, %T1], [%v2, %F1]
	br i1 %A, label %T2, label %F2

T2:
	call void @f3()
	ret i32 %B

F2:
	ret i32 %B
}


;; Lexically duplicated conditionals should be threaded.


define i32 @test6(i32 %A) {
; CHECK: @test6
	%tmp455 = icmp eq i32 %A, 42
	br i1 %tmp455, label %BB1, label %BB2
        
BB2:
; CHECK: call i32 @f1()
; CHECK-NEXT: call void @f3()
; CHECK-NEXT: ret i32 4
	call i32 @f1()
	br label %BB1
        

BB1:
	%tmp459 = icmp eq i32 %A, 42
	br i1 %tmp459, label %BB3, label %BB4

BB3:
	call i32 @f2()
        ret i32 3

BB4:
	call void @f3()
	ret i32 4
}


;; This tests that the branch in 'merge' can be cloned up into T1.
;; rdar://7367025
define i32 @test7(i1 %cond, i1 %cond2) {
Entry:
; CHECK: @test7
	%v1 = call i32 @f1()
	br i1 %cond, label %Merge, label %F1

F1:
	%v2 = call i32 @f2()
	br label %Merge

Merge:
	%B = phi i32 [%v1, %Entry], [%v2, %F1]
        %M = icmp ne i32 %B, %v1
        %N = icmp eq i32 %B, 47
        %O = and i1 %M, %N
	br i1 %O, label %T2, label %F2

; CHECK: Merge:
; CHECK-NOT: phi
; CHECK-NEXT:   %v2 = call i32 @f2()

T2:
	call void @f3()
	ret i32 %B

F2:
	ret i32 %B
; CHECK: F2:
; CHECK-NEXT: phi i32
}


declare i1 @test8a()

define i32 @test8b(i1 %cond, i1 %cond2) {
; CHECK: @test8b
T0:
        %A = call i1 @test8a()
	br i1 %A, label %T1, label %F1
        
; CHECK: T0:
; CHECK-NEXT: call
; CHECK-NEXT: br i1 %A, label %T1, label %Y

T1:
        %B = call i1 @test8a()
	br i1 %B, label %T2, label %F1

; CHECK: T1:
; CHECK-NEXT: call
; CHECK-NEXT: br i1 %B, label %T2, label %Y
T2:
        %C = call i1 @test8a()
	br i1 %cond, label %T3, label %F1

; CHECK: T2:
; CHECK-NEXT: call
; CHECK-NEXT: br i1 %cond, label %T3, label %Y
T3:
        ret i32 0

F1:
        %D = phi i32 [0, %T0], [0, %T1], [1, %T2]
        %E = icmp eq i32 %D, 1
        %F = and i1 %E, %cond
	br i1 %F, label %X, label %Y
X:
        call i1 @test8a()
        ret i32 1
Y:
        ret i32 2
}


;;; Verify that we can handle constraint propagation through "xor x, 1".
define i32 @test9(i1 %cond, i1 %cond2) {
Entry:
; CHECK: @test9
	%v1 = call i32 @f1()
	br i1 %cond, label %Merge, label %F1

; CHECK: Entry:
; CHECK-NEXT:  %v1 = call i32 @f1()
; CHECK-NEXT:  br i1 %cond, label %F2, label %Merge

F1:
	%v2 = call i32 @f2()
	br label %Merge

Merge:
	%B = phi i32 [%v1, %Entry], [%v2, %F1]
        %M = icmp eq i32 %B, %v1
        %M1 = xor i1 %M, 1
        %N = icmp eq i32 %B, 47
        %O = and i1 %M1, %N
	br i1 %O, label %T2, label %F2

; CHECK: Merge:
; CHECK-NOT: phi
; CHECK-NEXT:   %v2 = call i32 @f2()

T2:
	%Q = zext i1 %M to i32
	ret i32 %Q

F2:
	ret i32 %B
; CHECK: F2:
; CHECK-NEXT: phi i32
}



; CHECK: @test10
declare i32 @test10f1()
declare i32 @test10f2()
declare void @test10f3()

;; Non-local condition threading.
define i32 @test10g(i1 %cond) {
; CHECK: @test10g
; CHECK-NEXT:   br i1 %cond, label %T2, label %F2
        br i1 %cond, label %T1, label %F1

T1:
        %v1 = call i32 @test10f1()
        br label %Merge
        
; CHECK: %v1 = call i32 @test10f1()
; CHECK-NEXT: call void @f3()
; CHECK-NEXT: ret i32 %v1

F1:
        %v2 = call i32 @test10f2()
        br label %Merge

Merge:
        %B = phi i32 [%v1, %T1], [%v2, %F1]
        br i1 %cond, label %T2, label %F2

T2:
        call void @f3()
        ret i32 %B

F2:
        ret i32 %B
}


; Impossible conditional constraints should get threaded.  BB3 is dead here.
define i32 @test11(i32 %A) {
; CHECK: @test11
; CHECK-NEXT: icmp
; CHECK-NEXT: br i1 %tmp455, label %BB4, label %BB2
	%tmp455 = icmp eq i32 %A, 42
	br i1 %tmp455, label %BB1, label %BB2
        
BB2:
; CHECK: call i32 @f1()
; CHECK-NEXT: ret i32 %C
	%C = call i32 @f1()
	ret i32 %C
        

BB1:
	%tmp459 = icmp eq i32 %A, 43
	br i1 %tmp459, label %BB3, label %BB4

BB3:
	call i32 @f2()
        ret i32 3

BB4:
	call void @f3()
	ret i32 4
}

;; Correlated value through boolean expression.  GCC PR18046.
define void @test12(i32 %A) {
; CHECK: @test12
entry:
  %cond = icmp eq i32 %A, 0
  br i1 %cond, label %bb, label %bb1
; Should branch to the return block instead of through BB1.
; CHECK: entry:
; CHECK-NEXT: %cond = icmp eq i32 %A, 0
; CHECK-NEXT: br i1 %cond, label %bb1, label %return

bb:                   
  %B = call i32 @test10f2()
  br label %bb1

bb1:
  %C = phi i32 [ %A, %entry ], [ %B, %bb ]
  %cond4 = icmp eq i32 %C, 0
  br i1 %cond4, label %bb2, label %return

; CHECK: bb1:
; CHECK-NEXT: %B = call i32 @test10f2()
; CHECK-NEXT: %cond4 = icmp eq i32 %B, 0
; CHECK-NEXT: br i1 %cond4, label %bb2, label %return

bb2:
  %D = call i32 @test10f2()
  ret void

return:
  ret void
}


;; Duplicate condition to avoid xor of cond.
;; rdar://7391699
define i32 @test13(i1 %cond, i1 %cond2) {
Entry:
; CHECK: @test13
	%v1 = call i32 @f1()
	br i1 %cond, label %Merge, label %F1

F1:
	br label %Merge

Merge:
	%B = phi i1 [true, %Entry], [%cond2, %F1]
        %C = phi i32 [192, %Entry], [%v1, %F1]
        %M = icmp eq i32 %C, 192
        %N = xor i1 %B, %M
	br i1 %N, label %T2, label %F2

T2:
	ret i32 123

F2:
	ret i32 %v1
        
; CHECK:   br i1 %cond, label %F2, label %Merge

; CHECK:      Merge:
; CHECK-NEXT:   %M = icmp eq i32 %v1, 192
; CHECK-NEXT:   %N = xor i1 %cond2, %M
; CHECK-NEXT:   br i1 %N, label %T2, label %F2
}


