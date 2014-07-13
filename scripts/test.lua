io.write("HELLO WORLD\n")

function start_cmdstream(name)
  io.write("START: " .. name .. "\n")
end

function draw(primtype, nindx)
  io.write("DRAW: " .. primtype .. ", " .. nindx .. "\n")
  io.write("0x2280: written=" .. regs.written(0x2280) .. ", lastval=" .. regs.lastval(0x2280) .. ", val=" .. regs.val(0x2280) .. "\n")
end

function end_cmdstream()
  io.write("END\n")
end

function finish()
  io.write("FINISH\n")
end

