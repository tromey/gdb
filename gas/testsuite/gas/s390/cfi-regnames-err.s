	.text
	.cfi_startproc
# Invalid general register names
	.cfi_register r0, r16
	.cfi_register %r0, %r16
# Invalid floating-point register names
	.cfi_register f0, f16
	.cfi_register %f0, %f16
	.cfi_endproc
