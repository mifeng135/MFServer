local pb = require "pb"

local baseFmt = ">iHiic"
local baseHeaderLen = 14 --(i<4> + H<2> + i<4> + i<4>)

local baseRpcFmt = ">iijc"
local baseRpcHeaderLen = 16 --(i<4> + i<4> + j<8>)

---@class MFCoder
local MFCoder = {}

---------------------------------------<proto>-------------------------------
function MFCoder.protoEncode(msgId, data)
    local pbName = msgFactory[msgId]
    if not pbName then
        MF.core.log("MFCoder encode not find msgId = {}", msgId)
        return nil
    end
    return pb.encode(pbName, data)
end

function MFCoder.protoDecode(msgId, data)
    local pbName = msgFactory[msgId]
    if not pbName then
        MF.core.log("MFCoder decode not find msgId = {}", msgId)
        return
    end
    return pb.decode(pbName, data)
end

----------------------------------<tcp>---------------------------------------
function MFCoder.baseEncode(msgId, data, magic, msgSeq)
    local bodyLen = #data
    local fmt = baseFmt .. bodyLen ---(i = (H + i + i + pbLen)<2 + 4 + 4 + pbLen>) (---H magic ---i msgId ---i msgSeq ---c(len)bodyLen)
    local totalLen = baseHeaderLen + bodyLen
    return string.pack(fmt, totalLen, magic, msgId, msgSeq, data), totalLen
end

function MFCoder.baseDecode(data)
    local fmt = baseFmt .. (#data - baseHeaderLen)
    local _, magic, msgId, msgSeq, msgBody = string.unpack(fmt, data)
    return magic, msgId, msgSeq, msgBody
end

----------------------------------<rpc>-------------------------------------------
function MFCoder.baseRpcEncode(msgId, cSid, data)
    local bodyLen = #data
    local fmt = baseRpcFmt .. bodyLen ---(i = (i + j + pbLen)<4 + 8 + pbLen>) (---i msgId ---j cSid ---c(len)bodyLen)
    local totalLen = baseRpcHeaderLen + bodyLen
    return string.pack(fmt, totalLen, msgId, cSid, data), totalLen
end

function MFCoder.baseRpcDecode(data)
    local fmt = baseRpcFmt .. (#data - baseRpcHeaderLen)
    local _, msgId, cSid, msgBody = string.unpack(fmt, data)
    return msgId, cSid, msgBody
end

return MFCoder
