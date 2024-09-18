-- Copyright (c) 2024 Marc Balmer HB9SSB
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to
-- deal in the Software without restriction, including without limitation the
-- rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
-- sell copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
-- FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
-- IN THE SOFTWARE.

-- OpenRTX RTXLink protocol (http://openrtx.org/#/rtxlink)

local function slipWrite(s)
	trx.write(string.format('\xc0%s\xc0',
	    s:gsub('\xc0', '\xc0\xdc'):gsub('\xdb', '\xdb\xdd')))
end

local function slipRead()
end

local function initialize(driver)
	local payload = '\x01GIN'
	slipWrite(payload .. trx.crc16(payload))
	local resp = trx.read(19)
	print(resp:sub(4, -4))
end

local function setLock(driver)
	trx.write('\x00\x00\x00\x00\x00')
	return 'locked'
end

local function setUnlock(driver)
	trx.write('\x00\x00\x00\x00\x80')
	return 'unlocked'
end

local function setFrequency(driver, frequency)
	local freq = frequency
	print('set frequency to ' .. frequency)

	local byte4 = freq // 16777216
	freq = freq - byte4 * 16777216

	local byte3 = freq // 65536
	freq = freq - byte3 * 65536

	local byte2 = freq // 256
	freq = freq - byte2 * 256

	local byte1 = freq

	print(byte1, byte2, byte3, byte4)
	local payload = string.format('\x01SRF%s',
	     string.char(byte1, byte2, byte3, byte4))

	slipWrite(payload .. trx.crc16(payload))

	local ack = trx.read(8)

	return frequency
end

local function getFrequency(driver)
	local payload = '\x01GRF'
	slipWrite(payload .. trx.crc16(payload))
	local resp = trx.read(10)

	local frequency = resp:byte(7) * 16777216 + resp:byte(6) * 65536 +
	    resp:byte(5) * 256 + resp:byte(4)

	return frequency, 'M17'
end

local function setMode(driver, band, mode)
	return band, 'invalid mode ' .. mode
end

local function getMode(driver)
	return 'M17'
end

return {
	name = 'OpenRTX RTXLink protocol',
	capabilities = {	-- driver specific
		frequency = true,
		mode = false,
		lock = false
	},
	validModes = {},	-- trx specific
	ctcssModes = {},	-- trx specific
	statusUpdatesRequirePolling = true,
	initialize = initialize,
	startStatusUpdates = nil,
	stopStatusUpdates = nil,
	handleStatusUpdates = nil,
	setLock = setLock,
	setUnlock = setUnlock,
	setFrequency = setFrequency,
	getFrequency = getFrequency,
	getMode = getMode,
	setMode = setMode
}
