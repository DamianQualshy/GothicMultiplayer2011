LOG_INFO('[Dev][Shared] utility.lua initialized.')

	local start_tick = nil

local function log_elapsed()
	local elapsed = getTickCount() - start_tick
	LOG_INFO('{} ms elapsed since function call', elapsed)
end

function testUtility()
	LOG_WARN('[Dev][Shared] Demonstrating utility helpers')

	start_tick = getTickCount()
	print(string.format('Server tick at call: %d ms', start_tick))

	local color = hexToRgb('#7fc4ff')
	if color then
		local hex_without_prefix = rgbToHex(color.r, color.g, color.b)
		local hex_with_alpha = rgbToHex(color.r, color.g, color.b, color.a or 255, true, { uppercase = true })

		print(string.format('hexToRgb -> r=%d g=%d b=%d a=%d', color.r, color.g, color.b, color.a or 255))
		print(string.format('rgbToHex (no prefix) -> %s', hex_without_prefix))
		print(string.format('rgbToHex (with prefix + alpha) -> %s', hex_with_alpha))
	else
		LOG_WARN('Failed to decode color string')
	end

	local parsed = sscanf('7 1.25 "quoted value" true', '%d %f %q %b')
	if parsed then
		print(string.format('sscanf -> integer=%d, float=%.2f, text=%s, bool=%s', parsed[1], parsed[2], parsed[3], tostring(parsed[4])))
	else
		LOG_WARN('sscanf example failed to parse input')
	end

	setTimer(log_elapsed, 2500, 1)
end