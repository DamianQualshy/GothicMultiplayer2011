addEventHandler('onInit', function()
    LOG_WARN('[hash.lua] Demonstrating hash helpers')

    local string = "Hash Me"

    LOG_INFO('[hash.lua] Input: {}', string)
    LOG_INFO('[hash.lua]  md5   -> {}', md5(string))
    LOG_INFO('[hash.lua]  sha1  -> {}', sha1(string))
    LOG_INFO('[hash.lua]  sha256-> {}', sha256(string))
    LOG_INFO('[hash.lua]  sha384-> {}', sha384(string))
    LOG_INFO('[hash.lua]  sha512-> {}', sha512(string))
end)