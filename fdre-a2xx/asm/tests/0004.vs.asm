@varying(R0)    vTexCoord
@varying(R1)    vVaryingColor
@attribute(R1)  in_TexCoord
@attribute(R3)  in_normal
@attribute(R2)  in_position
@uniform(C0-C3) modelviewMatrix
@uniform(C4-C7) modelviewprojectionMatrix
@uniform(C8-C9) normalMatrix
@const(C11)     2.000000, 2.000000, 20.000000, 0.000000
@const(C12)     1.000000, 1.000000, 1.000000, 0.000000
@sampler(0)     uTexture
EXEC
      FETCH:  VERTEX  R1.xy11 = R0.z FMT_32_32_FLOAT SIGNED STRIDE(8) CONST(20, 0)
      FETCH:  VERTEX  R2.xyz1 = R0.x FMT_32_32_32_FLOAT SIGNED STRIDE(12) CONST(20, 1)
      FETCH:  VERTEX  R3.xyz_ = R0.y FMT_32_32_32_FLOAT SIGNED STRIDE(12) CONST(20, 2)
   (S)ALU:    MULv    R0 = R2.wwww, C7
      ALU:    MULADDv R0 = R0, R2.zzzz, C6
      ALU:    MULADDv R0 = R0, R2.yyyy, C5
ALLOC POSITION SIZE(0x0)
EXEC
      ALU:    MULADDv export62 = R0, R2.xxxx, C4    ; gl_Position
      ALU:    MULv    R0 = R2.wwww, C3
      ALU:    MULADDv R0 = R0, R2.zzzz, C2
      ALU:    MULADDv R0 = R0, R2.yyyy, C1
      ALU:    MULADDv R0 = R0, R2.xxxx, C0
      ALU:    MULv    R2.xyz_ = R3.zzzw, C10
              RECIP_IEEE     R4.x___ = R0
EXEC
      ALU:    MULADDv R0.xyz_ = C11.xxzw, -R0, R4.xxxw
      ALU:    DOT3v   R4.x___ = R0, R0
      ALU:    MULADDv R2.xyz_ = R2, R3.yyyw, C9
      ALU:    MULADDv R2.xyz_ = R2, R3.xxxw, C8
ALLOC PARAM/PIXEL SIZE(0x1)
EXEC_END
      ALU:    MAXv    export0 = R1, R1
      ALU:    MAXv    R0.____ = R0, R0
              RECIPSQ_IEEE     R0.___w = R4.xyzx
      ALU:    MULv    R0.xyz_ = R0, R0.wwww
      ALU:    DOT3v   R0.x___ = R2, R0
      ALU:    MAXv    export1.xyz_ = R0.xxxw, C11.wwww
      ALU:    MAXv    export1.___w = C12.yxzx, C12.yxzx

