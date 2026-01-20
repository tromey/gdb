	.text
# General register (GR) names r0..r15
	.type gr, @function
gr:
	.cfi_startproc
	.word 0
	.cfi_register r0, r1
	.cfi_register r2, r3
	.cfi_register r4, r5
	.cfi_register r6, r7
	.cfi_register r8, r9
	.cfi_register r10, r11
	.cfi_register r12, r13
	.cfi_register r14, r15
	.word 0
	.cfi_endproc
	.size gr, .-gr
# Floating-point register (FPR) names f0..f15
	.type fpr, @function
fpr:
	.cfi_startproc
	.word 0
	.cfi_register f0, f1
	.cfi_register f2, f3
	.cfi_register f4, f5
	.cfi_register f6, f7
	.cfi_register f8, f9
	.cfi_register f10, f11
	.cfi_register f12, f13
	.cfi_register f14, f15
	.word 0
	.cfi_endproc
	.size fpr, .-fpr
