PUBLIC mov_to_r15
PUBLIC noop

.code

mov_to_r15 PROC
	mov r15, rcx
	ret
mov_to_r15 ENDP

noop PROC
	ret
noop ENDP

END