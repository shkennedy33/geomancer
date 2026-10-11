local dir = "/mnt/fast/projects/VST/build/reaper-test/"
local log = io.open(dir .. "build.log", "w")
local f = io.open(dir .. "state.b64", "r"); local chunk = f:read("*a"); f:close()

reaper.SetCurrentBPM(0, 120, false)
reaper.InsertTrackAtIndex(0, true)
reaper.InsertTrackAtIndex(1, true)
local tr = reaper.GetTrack(0, 0)
local stem = reaper.GetTrack(0, 1)
reaper.GetSetMediaTrackInfo_String(tr, "P_NAME", "Geomancer", true)
reaper.GetSetMediaTrackInfo_String(stem, "P_NAME", "Snare stem", true)
local fx = reaper.TrackFX_AddByName(tr, "Geomancer", false, -1)
log:write("fx " .. fx .. "\n")
log:write("set chunk: " .. tostring(reaper.TrackFX_SetNamedConfigParm(tr, fx, "vst_chunk", chunk)) .. "\n")

-- 4 track channels so plugin outputs 3/4 (Out 1) land somewhere, then send them to the stem track
reaper.SetMediaTrackInfo_Value(tr, "I_NCHAN", 4)
local send = reaper.CreateTrackSend(tr, stem)
reaper.SetTrackSendInfo_Value(tr, 0, send, "I_SRCCHAN", 2)  -- source channels 3/4
reaper.SetTrackSendInfo_Value(tr, 0, send, "I_DSTCHAN", 0)
reaper.SetMediaTrackInfo_Value(stem, "B_MAINSEND", 0)       -- keep the master = kick only

-- MIDI item: pattern select notes (0 = P1, 1 = P2) at bars 1, 3, 5
local item = reaper.CreateNewMIDIItemInProj(tr, 0, 16, false)
local take = reaper.GetActiveTake(item)
local function note(bar, pitch)
  local qn = (bar - 1) * 4
  local p0 = reaper.MIDI_GetPPQPosFromProjQN(take, qn)
  local p1 = reaper.MIDI_GetPPQPosFromProjQN(take, qn + 0.25)
  reaper.MIDI_InsertNote(take, false, false, p0, p1, 0, pitch, 100, true)
end
note(1, 0); note(3, 1); note(5, 0)
reaper.MIDI_Sort(take)

-- render: master + stem of the selected stem track, 8 bars
reaper.SetOnlyTrackSelected(stem)
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 1, true)      -- master mix + stems (selected tracks)
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 0, true)    -- custom time bounds
reaper.GetSetProjectInfo(0, "RENDER_STARTPOS", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_ENDPOS", 16, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", 48000, true)
reaper.GetSetProjectInfo(0, "RENDER_TAILFLAG", 0, true)
reaper.GetSetProjectInfo_String(0, "RENDER_FILE", dir .. "render", true)
reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", "$track", true)
reaper.Main_SaveProjectEx(0, dir .. "geomancer_test.rpp", 0)
log:write("saved\n")
log:close()
