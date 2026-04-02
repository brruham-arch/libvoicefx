-- voicechanger.lua v2.0
-- Engine: libvoicefx.so (C TD-PSOLA pitch shifter)
-- MoNetLoader 3.6.0 | Lua 5.1
-- Controller only — semua DSP di .so

local ffi = require("ffi")

-- ============================================================
-- FFI DECLARATIONS
-- ============================================================
ffi.cdef[[
    -- Dobby (hook)
    void* DobbySymbolResolver(const char* image, const char* symbol);
    int   DobbyHook(void* addr, void* replace, void** origin);

    -- BASS
    unsigned int BASS_RecordStart(unsigned int freq, unsigned int chans,
                                  unsigned int flags, void* proc, void* user);
    unsigned int BASS_ChannelSetDSP(unsigned int handle, void* proc,
                                    void* user, int priority);
    unsigned int BASS_ChannelRemoveDSP(unsigned int handle, unsigned int dsp);

    -- libvoicefx API
    void  vc_init(int sample_rate, int channels);
    void  vc_set_pitch(float factor);
    void  vc_enable(void);
    void  vc_disable(void);
    int   vc_is_enabled(void);
    float vc_get_pitch(void);
    void  vc_process(int16_t* buf, int n);
    void  vc_destroy(void);

    typedef int16_t int16_t;
]]

-- ============================================================
-- LOAD ENGINE .so
-- ============================================================
local vc = nil
local function loadEngine()
    -- Coba beberapa path umum
    local paths = {
        "libvoicefx.so",
        "/sdcard/MoNetLoader/libvoicefx.so",
        "/sdcard/libvoicefx.so",
        "/data/local/tmp/libvoicefx.so",
    }
    for _, path in ipairs(paths) do
        local ok, lib = pcall(ffi.load, path)
        if ok then
            sampAddChatMessage("[VFX] Engine loaded: " .. path, 0x00FF88)
            return lib
        end
    end
    sampAddChatMessage("[VFX] ERROR: libvoicefx.so tidak ditemukan!", 0xFF4444)
    sampAddChatMessage("[VFX] Taruh libvoicefx.so di /sdcard/MoNetLoader/", 0xFFFF00)
    return nil
end

-- ============================================================
-- STATE
-- ============================================================
_G.VFX = {
    recHandle  = 0,
    dspHandle  = 0,
    orig_RS    = ffi.new("void*[1]"),
    sampleRate = 8000,   -- SAMP default voice sample rate
    channels   = 1,
}
local VFX = _G.VFX

-- ============================================================
-- DSP CALLBACK — tipis, hanya forward ke C engine
-- ============================================================
_G.VFX.dspProc = ffi.cast(
    "void(*)(unsigned int, unsigned int, void*, unsigned int, void*)",
    function(dsp, channel, buf, len, user)
        if vc == nil then return end
        local s16 = ffi.cast("int16_t*", buf)
        local n   = math.floor(len / 2)
        vc.vc_process(s16, n)
    end
)

-- ============================================================
-- HOOK: BASS_RecordStart
-- ============================================================
_G.VFX.hook_RS = ffi.cast(
    "unsigned int(*)(unsigned int,unsigned int,unsigned int,void*,void*)",
    function(freq, chans, flags, proc, user)
        local orig = ffi.cast(
            "unsigned int(*)(unsigned int,unsigned int,unsigned int,void*,void*)",
            VFX.orig_RS[0])
        local handle = orig(freq, chans, flags, proc, user)
        VFX.recHandle   = handle
        VFX.sampleRate  = freq
        VFX.channels    = chans
        sampAddChatMessage("[VFX] RecHandle=" .. handle .. " freq=" .. freq, 0x00FF88)
        -- Init engine dengan sample rate yang benar
        if vc then vc.vc_init(freq, chans) end
        return handle
    end
)

-- ============================================================
-- HELPERS
-- ============================================================
local bass = nil
local function getBass()
    if bass == nil then bass = ffi.load("libBASS.so") end
    return bass
end

local function attachDSP()
    if VFX.recHandle == 0 then
        sampAddChatMessage("[VFX] Belum ada RecHandle, tekan talk dulu!", 0xFFFF00)
        return false
    end
    if VFX.dspHandle ~= 0 then return true end
    local b   = getBass()
    local dsp = b.BASS_ChannelSetDSP(VFX.recHandle, _G.VFX.dspProc, nil, 1)
    if dsp ~= 0 then
        VFX.dspHandle = dsp
        sampAddChatMessage("[VFX] DSP terpasang", 0x00FF88)
        return true
    else
        sampAddChatMessage("[VFX] DSP gagal dipasang", 0xFF4444)
        return false
    end
end

local function detachDSP()
    if VFX.dspHandle == 0 then return end
    getBass().BASS_ChannelRemoveDSP(VFX.recHandle, VFX.dspHandle)
    VFX.dspHandle = 0
    sampAddChatMessage("[VFX] DSP dilepas", 0xFF8800)
end

-- ============================================================
-- MAIN
-- ============================================================
function main()
    while not isSampAvailable() do wait(100) end
    wait(1000)

    sampAddChatMessage("[VoiceFX] v2.0 loading...", 0xFFFF00)

    -- Load C engine
    vc = loadEngine()
    if vc == nil then return end

    -- Init engine default
    vc.vc_init(8000, 1)

    -- Hook BASS_RecordStart
    local dobby = ffi.load("libdobby.so")
    local addr_RS = dobby.DobbySymbolResolver("libBASS.so", "BASS_RecordStart")
    if addr_RS == nil then
        sampAddChatMessage("[VFX] ERROR: BASS_RecordStart tidak ditemukan", 0xFF4444)
        return
    end
    if dobby.DobbyHook(addr_RS, VFX.hook_RS, VFX.orig_RS) ~= 0 then
        sampAddChatMessage("[VFX] ERROR: Hook BASS gagal", 0xFF4444)
        return
    end

    sampAddChatMessage("[VFX] Hook OK!", 0x00FF88)
    sampAddChatMessage("[VFX] /vfx = toggle | /vfp [0.3-3.0] = pitch", 0x00FFFF)

    -- ============================================================
    -- COMMANDS
    -- ============================================================

    -- Toggle ON/OFF
    sampRegisterChatCommand("vfx", function()
        if vc == nil then return end
        if vc.vc_is_enabled() == 0 then
            vc.vc_enable()
            if attachDSP() then
                sampAddChatMessage("[VFX] ON (pitch=" .. vc.vc_get_pitch() .. ")", 0x00FF88)
            end
        else
            vc.vc_disable()
            detachDSP()
            sampAddChatMessage("[VFX] OFF", 0xFF8800)
        end
    end)

    -- Set pitch
    sampRegisterChatCommand("vfp", function(arg)
        if vc == nil then return end
        local v = tonumber(arg)
        if v and v >= 0.3 and v <= 3.0 then
            vc.vc_set_pitch(v)
            local desc = v > 1.0 and "lebih tinggi" or v < 1.0 and "lebih rendah/berat" or "normal"
            sampAddChatMessage("[VFX] Pitch = " .. v .. " (" .. desc .. ")", 0x00FFFF)
        else
            if vc then
                sampAddChatMessage("[VFX] /vfp [0.3-3.0] | sekarang=" .. vc.vc_get_pitch(), 0xFFFF00)
            end
        end
    end)

    -- Status
    sampRegisterChatCommand("vfs", function()
        if vc == nil then
            sampAddChatMessage("[VFX] Engine tidak loaded", 0xFF4444)
            return
        end
        local st = vc.vc_is_enabled() == 1 and "ON" or "OFF"
        sampAddChatMessage("[VFX] Status=" .. st ..
            " | Pitch=" .. vc.vc_get_pitch() ..
            " | DSP=" .. VFX.dspHandle ..
            " | Rec=" .. VFX.recHandle, 0x00FFFF)
    end)

    while true do wait(1000) end
end
