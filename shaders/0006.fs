precision mediump float;

uniform sampler2D g_NormalMap;
varying vec2 vTexCoord0;

void main()
{
  vec3 vNormal = vec3(2.0, 2.0, 0.0) * texture2D(g_NormalMap, vTexCoord0).rgb;
  vNormal.z = sqrt(1.0 - dot(vNormal, vNormal));
  gl_FragColor = vec4(vNormal, 1.0);
}

/* according to a screenshot in optimize-for-adreno.pdf, this corresponds to roughly:

Notes from AMD asm language format doc.. which is not the same but seems to have
some similar aspects:

ADDR is a quadword (64b) address specified in the microcode; it is not a byte address.
CNT is in units of 64b slots.

00	EXEC ADDR(0x2) CNT(0x4) SERIALIZE(0x9)
	0    FETCH:	SAMPLE	R0.xyz_ = R0.xyz  CONST(0)
	0 (?)ALU:	MULADDv	R0.xyz_ = R0, C0.??, -C1.??  (??? why -C1.swiz?)
	1    ALU:	DOT3v	R1.x___ = R0, R0
	2    ALU:	ADDv	R1.x___ = -R1, C1
01	ALLOC PARAM/PIXEL SIZE(0x0)
02	EXEC_END ADDR(0x6) CNT(0x2)
	3    ALU:	MAXv	export0.zy__ = R0, R0  (??? maybe MOV is an alias for MAX foo = Rn, Rn ???, or maybe next line is part of same instr?)
		SQRT_IEEE	export0.__z_ = R1.xyzx
	4    ALU:	MAXv	export0.___w = C1.xyzx, C1.xyzx
03	NOP

(to the best of my ability to read that screenshot, and infer what is truncated)

I assume:
  C0: vec3(2.0, 2.0, 0.0)
  C1: 1.0

*/
