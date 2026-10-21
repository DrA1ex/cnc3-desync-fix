# setFPMode entry probe. TABLE/TABLE_N are absolute addresses passed via --defsym.
	.intel_syntax noprefix
	.text
probe:
	pushfd
	pushad
	sub esp, 8
	fnstcw word ptr [esp]
	stmxcsr dword ptr [esp+4]
	mov ebx, dword ptr [esp+44]
	movzx ecx, word ptr [esp]
	mov edx, dword ptr [esp+4]
	mov esi, TABLE
	xor edi, edi
1:
	cmp edi, dword ptr [TABLE_N]
	jae 3f
	cmp dword ptr [esi], ebx
	jne 2f
	cmp dword ptr [esi+4], ecx
	jne 2f
	cmp dword ptr [esi+8], edx
	jne 2f
	inc dword ptr [esi+12]
	jmp 4f
2:
	add esi, 16
	inc edi
	jmp 1b
3:
	cmp edi, 250
	jae 4f
	mov dword ptr [esi], ebx
	mov dword ptr [esi+4], ecx
	mov dword ptr [esi+8], edx
	mov dword ptr [esi+12], 1
	inc dword ptr [TABLE_N]
4:
	inc dword ptr [TABLE_N+4]
	add esp, 8
	popad
	popfd
	call dword ptr [0xa25460]
	push 0x80927a
	ret
