LOG_INFO('[Dev][Shared] hash.lua initialized.')

function testHash()
	LOG_WARN('[Dev][Shared] Demonstrating hash helpers:')

	local string = "Hash Me"
	local payloads = { string }

	for _, text in ipairs(payloads) do
		print(string.format('Input: %s', text))
		print(string.format('sha256-> %s', sha256(text)))
		print(string.format('sha512-> %s', sha512(text)))
	end
end
