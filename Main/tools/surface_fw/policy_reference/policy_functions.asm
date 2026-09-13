; SAM variant 0: reviewed mapping and index writes. Analyst function names.
; read_setting_and_map_to_pair: [0xd8584, 0xd85e8)
   d8584: b580         	push	{r7, lr}
   d8586: 460b         	mov	r3, r1
   d8588: 2201         	movs	r2, #0x1
   d858a: f100 0108    	add.w	r1, r0, #0x8
   d858e: 1c58         	adds	r0, r3, #0x1
   d8590: f7b8 fc23    	bl	0x90dda <.text+0x10dda> @ imm = #-0x477ba
   d8594: f04f 4000    	mov.w	r0, #0x80000000
   d8598: bd02         	pop	{r1, pc}
   d859a: f04f 4200    	mov.w	r2, #0x80000000
   d859e: 0003         	movs	r3, r0
   d85a0: d103         	bne	0xd85aa <.text+0x585aa> @ imm = #0x6
   d85a2: 2064         	movs	r0, #0x64
   d85a4: 7008         	strb	r0, [r1]
   d85a6: 7048         	strb	r0, [r1, #0x1]
   d85a8: e01c         	b	0xd85e4 <.text+0x585e4> @ imm = #0x38
   d85aa: 2b27         	cmp	r3, #0x27
   d85ac: d204         	bhs	0xd85b8 <.text+0x585b8> @ imm = #0x8
   d85ae: 2011         	movs	r0, #0x11
   d85b0: 7008         	strb	r0, [r1]
   d85b2: 200d         	movs	r0, #0xd
   d85b4: 7048         	strb	r0, [r1, #0x1]
   d85b6: e015         	b	0xd85e4 <.text+0x585e4> @ imm = #0x2a
   d85b8: 2b40         	cmp	r3, #0x40
   d85ba: d204         	bhs	0xd85c6 <.text+0x585c6> @ imm = #0x8
   d85bc: 2015         	movs	r0, #0x15
   d85be: 7008         	strb	r0, [r1]
   d85c0: 200f         	movs	r0, #0xf
   d85c2: 7048         	strb	r0, [r1, #0x1]
   d85c4: e00e         	b	0xd85e4 <.text+0x585e4> @ imm = #0x1c
   d85c6: 2b59         	cmp	r3, #0x59
   d85c8: d204         	bhs	0xd85d4 <.text+0x585d4> @ imm = #0x8
   d85ca: 2018         	movs	r0, #0x18
   d85cc: 7008         	strb	r0, [r1]
   d85ce: 2011         	movs	r0, #0x11
   d85d0: 7048         	strb	r0, [r1, #0x1]
   d85d2: e007         	b	0xd85e4 <.text+0x585e4> @ imm = #0xe
   d85d4: 2865         	cmp	r0, #0x65
   d85d6: d204         	bhs	0xd85e2 <.text+0x585e2> @ imm = #0x8
   d85d8: 2024         	movs	r0, #0x24
   d85da: 7008         	strb	r0, [r1]
   d85dc: 201d         	movs	r0, #0x1d
   d85de: 7048         	strb	r0, [r1, #0x1]
   d85e0: e000         	b	0xd85e4 <.text+0x585e4> @ imm = #0x0
   d85e2: 4a25         	ldr	r2, [pc, #0x94]         @ 0xd8678 <.text+0x58678> ; literal=0x8300001f
   d85e4: 4610         	mov	r0, r2
   d85e6: 4770         	bx	lr

; mailbox_index_trigger: [0xdcfd8, 0xdcff2)
   dcfd8: b53e         	push	{r1, r2, r3, r4, r5, lr}
   dcfda: 4a6f         	ldr	r2, [pc, #0x1bc]        @ 0xdd198 <.text+0x5d198> ; literal=0x0008e7e8
   dcfdc: e9d2 4500    	ldrd	r4, r5, [r2]
   dcfe0: e9cd 4500    	strd	r4, r5, [sp]
   dcfe4: 0609         	lsls	r1, r1, #0x18
   dcfe6: 9101         	str	r1, [sp, #0x4]
   dcfe8: 2208         	movs	r2, #0x8
   dcfea: 4669         	mov	r1, sp
   dcfec: f7b3 fa04    	bl	0x903f8 <.text+0x103f8> @ imm = #-0x4cbf8
   dcff0: bd3e         	pop	{r1, r2, r3, r4, r5, pc}

; write_press_release_index_addresses: [0xdd086, 0xdd0ba)
   dd086: b53e         	push	{r1, r2, r3, r4, r5, lr}
   dd088: 4a48         	ldr	r2, [pc, #0x120]        @ 0xdd1ac <.text+0x5d1ac> ; literal=0x0008e7f0
   dd08a: e9d2 4500    	ldrd	r4, r5, [r2]
   dd08e: e9cd 4500    	strd	r4, r5, [sp]
   dd092: 0609         	lsls	r1, r1, #0x18
   dd094: 9101         	str	r1, [sp, #0x4]
   dd096: 2208         	movs	r2, #0x8
   dd098: 4669         	mov	r1, sp
   dd09a: f7b3 f9ad    	bl	0x903f8 <.text+0x103f8> @ imm = #-0x4cca6
   dd09e: bd3e         	pop	{r1, r2, r3, r4, r5, pc}
   dd0a0: b53e         	push	{r1, r2, r3, r4, r5, lr}
   dd0a2: 4a43         	ldr	r2, [pc, #0x10c]        @ 0xdd1b0 <.text+0x5d1b0> ; literal=0x0008e7f8
   dd0a4: e9d2 4500    	ldrd	r4, r5, [r2]
   dd0a8: e9cd 4500    	strd	r4, r5, [sp]
   dd0ac: 0609         	lsls	r1, r1, #0x18
   dd0ae: 9101         	str	r1, [sp, #0x4]
   dd0b0: 2208         	movs	r2, #0x8
   dd0b2: 4669         	mov	r1, sp
   dd0b4: f7b3 f9a0    	bl	0x903f8 <.text+0x103f8> @ imm = #-0x4ccc0
   dd0b8: bd3e         	pop	{r1, r2, r3, r4, r5, pc}
