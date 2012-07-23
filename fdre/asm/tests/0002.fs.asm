EXEC ADDR(0x2) CNT(0x1)
  (S)ALU:	MAXv	R0.____ = R0, R0
    	SQRT	R2.x___ = R1.x
ALLOC PARAM/PIXEL SIZE(0x0)
EXEC_END ADDR(0x3) CNT(0x3)
     ALU:	MAXv	export0.xy__ = R0, R0	; gl_FragColor
     ALU:	MAXv	R0.____ = R0, R0
    	SQRT	R2._y__ = R1.y
     ALU:	MAXv	export0.__zw = R2.xyxy, R2.xyxy	; gl_FragColor
NOP
