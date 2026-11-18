# Apeiron.pef - name every function Ghidra left as FUN_/undefined.
#
# Run it from Ghidra's Script Manager with Apeiron.pef open (Window > Script Manager, then the
# green arrow; put this file in a directory listed under "Manage Script Directories"). It expects
# the PEF's code section at 0x10000000, which is where Ghidra's PEF loader puts it - the script
# says so and stops if the addresses do not land in the program.
#
# The names come out of the PowerPC traceback tables the compiler left after each function, which
# tools/pef_symbols.py reads straight out of the application's data fork. They are the same names
# Ghidra shows on the functions it did recognise, so nothing here is invented.
#
# Of the 250 it names, the ones worth going to first are the per-sprite routines: every creature
# keeps a frame routine at +0x08 of its sprite and a movement routine at +0x0c, and that is where
# all the behaviour the decompile does not show actually lives.
#
# sprite                +0x08 pointer -> frame proc           +0x0c pointer -> move proc
#  gecko (Gordon)      0x1001caf8 -> GeckoFrameProc         0x1001cafc -> GeckoMoveProc
#  scorpion (Larry)    0x1001caf0 -> ScorpianFrameProc      0x1001caf4 -> ScorpianMoveProc
#  fodder / the UFO    0x1001ca90 -> FodderFrameProc        0x1001ca94 -> FodderMoveProc
#  flea                0x1001ca88 -> FleaFrameProc          0x1001ca8c -> FleaMoveProc
#  player's shot       0x1001cb54 -> ShotFrameProc          0x1001cb58 -> ShotMoveProc
#  yummie coin         0x1001ca78 -> YummieFrameProc        0x1001ca7c -> YummieMoveProc
#  uprooted shroom     0x1001caa8 -> RootFrameProc          0x1001caac -> RootMoveProc
#  multiplier tile     0x1001ca98 -> MultFrameProc          0x1001ca9c -> MultMoveProc
#  psychedelic shroom  0x1001caa0 -> PsychFrameProc         0x1001caa4 -> PsychMoveProc
#  explosion           0x1001ca80 -> ExploFrameProc         0x1001ca84 -> ExploMoveProc
#  ricochet            0x1001ca6c -> RicochetFrameProc      0x1001ca70 -> RicochetMoveProc
#  flak                0x1001cac0 -> FlakFrameProc          0x1001cac4 -> FlakMoveProc
#  rising points       0x1001cab4 -> PointsFrameProc        0x1001cab8 -> PointsMoveProc
#  scum splash         0x1001cacc -> ScumFrameProc          0x1001cad0 -> ScumMoveProc
#  shield              0x1001cb7c -> ShieldFrameProc        0x1001cb80 -> ShieldMoveProc
#  pede head           0x1001cb68 -> PedeMoveProc           0x1001cb6c -> (null)
#  pede body           0x1001cb60 -> (null)                 0x1001cb64 -> PedeFrameProc

from ghidra.program.model.symbol import SourceType

SYMBOLS = [
    (0x1000001c, "ZappaTribute"),
    (0x10000120, "ScoresFilter"),
    (0x100002a4, "EnterHighScore"),
    (0x100005ec, "DrawCredit"),
    (0x100006d0, "AboutFilter"),
    (0x100008f0, "DoAbout"),
    (0x10000a58, "ClearScores"),
    (0x10000c6c, "DoHelp"),
    (0x10000d68, "AboutWindowIdle"),
    (0x10000df0, "AboutWindowClick"),
    (0x10000fc8, "AboutWindowUpdate"),
    (0x100011c8, "CreateSprites"),
    (0x100013ec, "WasteSprites"),
    (0x1000147c, "MakeASprite"),
    (0x10001548, "MakeCoin"),
    (0x10001678, "myGeckoFrameProc"),
    (0x100016c0, "NoFrameProc"),
    (0x100016e4, "GenericFrameProc"),
    (0x10001730, "MushFrameProc"),
    (0x10001778, "HeadFrameProc"),
    (0x100017bc, "LinkFrameProc"),
    (0x10001800, "GenericMoveProc"),
    (0x10001828, "BounceMoveProc"),
    (0x10001998, "NewGame"),
    (0x10001c18, "DoShroomCheck"),
    (0x1000218c, "HandleInput"),
    (0x10002330, "GameLoop"),
    (0x10002750, "DrawWeapon"),
    (0x1000282c, "DrawMult"),
    (0x10002914, "UpdateWeapons"),
    (0x10002abc, "DrawLives"),
    (0x10002bc0, "DrawNum"),
    (0x10002cc4, "DrawNumDisplay"),
    (0x10002dd4, "IsPressed"),
    (0x10002e44, "AbortGame"),
    (0x10002e9c, "GameActivate"),
    (0x10002f0c, "GameDeactivate"),
    (0x10002f58, "GameOver"),
    (0x10003424, "ScoreContest"),
    (0x100035f8, "UpdateScore"),
    (0x10003868, "GameHouseKeeping"),
    (0x10004000, "MakeInvincible"),
    (0x10004104, "WasteShrooms"),
    (0x100041c4, "HandleSideFeed"),
    (0x100043bc, "ShroomShotCheck"),
    (0x10004550, "FleaDescentCheck"),
    (0x100048b4, "PauseGame"),
    (0x10004b48, "CountOffShrooms"),
    (0x10004da4, "TallyBonus"),
    (0x1000500c, "StartLevel"),
    (0x10005bd0, "CreateShroomField"),
    (0x10005d04, "PlantShroom"),
    (0x10005eac, "PluckShroom"),
    (0x10005fe0, "SetLevelBackground"),
    (0x100060a4, "CapsLockDown"),
    (0x100060fc, "GameWindowUpdate"),
    (0x10006454, "CreateScumSplash"),
    (0x100065a0, "CreateFlak"),
    (0x10006714, "CreatePoints"),
    (0x100068c8, "CreateBonusPoints"),
    (0x10006a80, "CreateUproot"),
    (0x10006bc0, "CreatePsychadelic"),
    (0x10006d04, "CreateMultiplier"),
    (0x10006e54, "CreateFodder"),
    (0x10006fdc, "CreateFlea"),
    (0x100071c8, "CreateExplosion"),
    (0x10007300, "CreateYummie"),
    (0x100074a4, "CreateRicochet"),
    (0x10007594, "ShroomMoveProc"),
    (0x100075bc, "ShroomFrameProc"),
    (0x100075e4, "FleaMoveProc"),
    (0x100078bc, "FleaFrameProc"),
    (0x10007904, "GeckoMoveProc"),
    (0x10007a10, "GeckoFrameProc"),
    (0x10007a58, "ScorpianMoveProc"),
    (0x10007da8, "ScumFrameProc"),
    (0x10007e20, "ScumMoveProc"),
    (0x10007f00, "YummieMoveProc"),
    (0x100080f8, "YummieFrameProc"),
    (0x10008144, "FlakFrameProc"),
    (0x10008190, "FlakMoveProc"),
    (0x1000826c, "PointsFrameProc"),
    (0x10008294, "PointsMoveProc"),
    (0x100083bc, "PsychFrameProc"),
    (0x10008408, "PsychMoveProc"),
    (0x1000848c, "MultFrameProc"),
    (0x100084b0, "MultMoveProc"),
    (0x10008534, "ShieldFrameProc"),
    (0x10008670, "ShieldMoveProc"),
    (0x100086b8, "FodderFrameProc"),
    (0x10008704, "FodderMoveProc"),
    (0x10008930, "RicochetFrameProc"),
    (0x10008958, "RicochetMoveProc"),
    (0x100089f4, "RootFrameProc"),
    (0x10008a18, "RootMoveProc"),
    (0x10008b2c, "ExploFrameProc"),
    (0x10008bac, "ExploMoveProc"),
    (0x10008bd0, "ScorpianFrameProc"),
    (0x10008c18, "PedeMoveProc"),
    (0x100095d8, "PedeFrameProc"),
    (0x100096c8, "ShooterMoveProc"),
    (0x10009a98, "ShooterFrameProc"),
    (0x10009acc, "ShotMoveProc"),
    (0x10009bb4, "ShotFrameProc"),
    (0x10009bfc, "ShotHitCheck"),
    (0x1000a8b8, "HandleInitializations"),
    (0x1000ad14, "ADBLookup"),
    (0x1000ad44, "InitializeAnimation"),
    (0x1000b074, "ApeironIntro"),
    (0x1000b694, "FunkyFade"),
    (0x1000b790, "LoadApeironSounds"),
    (0x1000be90, "CompileSingleDraw"),
    (0x1000bf9c, "CompileNumbers"),
    (0x1000c420, "LoadApeironSprites"),
    (0x1000d00c, "DrawPictInWindow"),
    (0x1000d120, "OpenAuxFiles"),
    (0x1000d4d4, "CenterRectInRect"),
    (0x1000d5b4, "TransmogrifyClut"),
    (0x1000d6e0, "MySleepProc"),
    (0x1000d760, "MyInstallSleepProc"),
    (0x1000d7e0, "main"),
    (0x1000d964, "MainLoop"),
    (0x1000da74, "PeriodicTasks"),
    (0x1000daf0, "CleanUp"),
    (0x1000dce0, "HandleActivate"),
    (0x1000dde8, "HandleDeactivate"),
    (0x1000deb0, "HandleEvent"),
    (0x1000dfb0, "DoKey"),
    (0x1000e554, "UpdateWindow"),
    (0x1000e760, "PauseUpdate"),
    (0x1000e8a4, "CopyPString"),
    (0x1000e8fc, "UpdateTitleScreen"),
    (0x1000f50c, "HandleClick"),
    (0x1000f5fc, "ContentClick"),
    (0x1000f6d4, "TitleClick"),
    (0x1000fca4, "SetPrefs"),
    (0x1000fdcc, "TrackCIcon"),
    (0x1000ff04, "DoCommand"),
    (0x10010044, "ColorError"),
    (0x100100a0, "ReportError"),
    (0x10010170, "ReportStr"),
    (0x10010190, "UpdateMainWindow"),
    (0x100101dc, "ShowSharewareNotice"),
    (0x10010244, "OpenWidgetTool"),
    (0x10010328, "CloseWidgetTool"),
    (0x100103bc, "FastRandom"),
    (0x100104c4, "CenterWindow"),
    (0x10010628, "HasColorQD"),
    (0x10010694, "WidgetHideMenuBar"),
    (0x100107dc, "WidgetShowMenuBar"),
    (0x1001090c, "PreventScreenSaver"),
    (0x10010978, "ZoomShowWindow"),
    (0x10010be0, "InitToolbox"),
    (0x10010ce8, "SetMouse"),
    (0x10010d08, "InitMBarRoutines"),
    (0x10010eac, "OpenAnimationTool"),
    (0x100110a0, "ATDisposeSprite"),
    (0x1001118c, "ATNewSprite"),
    (0x100112b4, "ATHideSprite"),
    (0x100112e0, "ATShowSprite"),
    (0x1001130c, "NewLayer"),
    (0x100113c4, "SetPortToBackground"),
    (0x10011434, "RestorePortFromBackground"),
    (0x10011494, "ClipRectToGameArea"),
    (0x1001153c, "CopyBitsCT"),
    (0x100115cc, "CompositeFrame"),
    (0x10011b90, "DirtyAnimation"),
    (0x10011bfc, "DirtyScreen"),
    (0x10011c2c, "AnimateFrame"),
    (0x10011d80, "UpdateStage"),
    (0x10011e20, "ResetAnimation"),
    (0x10011e48, "CollisionCheck"),
    (0x10011f4c, "FindFaceRect"),
    (0x10012000, "CompileSprite"),
    (0x100121c8, "CompileSpriteDraw"),
    (0x1001236c, "HaltSounds"),
    (0x100123ac, "STLoopCallBack"),
    (0x10012430, "STPlayLoopedSound"),
    (0x100124ec, "StartMusic"),
    (0x10012568, "HaltMusic"),
    (0x100125c8, "HaltCursorAnimation"),
    (0x100125f4, "SBShowHideControlStrip"),
    (0x10012664, "SBIsControlStripVisible"),
    (0x100126d0, "ST_Open"),
    (0x10012944, "ST_Close"),
    (0x100129e4, "ST_SetVolume"),
    (0x10012a7c, "ST_GetSysVolume"),
    (0x10012b20, "ST_SimplePlay"),
    (0x10012bc4, "ST_PlaySound"),
    (0x10012fbc, "ST_HaltSound"),
    (0x10013218, "ST_IsSoundPlaying"),
    (0x100132c4, "ST_LoadSound"),
    (0x10013348, "ST_PrepareSound"),
    (0x100134a8, "HaltASound"),
    (0x10013654, "DoubleBufferCallBack"),
    (0x100137dc, "StereoMixerProcPPC"),
    (0x1001392c, "MonoMixerProcPPC"),
    (0x10013a40, "InitSTVars"),
    (0x10013c3c, "CreateDoubleBuffer"),
    (0x10013d14, "CreateSndChannel"),
    (0x10013d80, "MyTrapAvailable"),
    (0x10013e8c, "GetSndHeaderOffset"),
    (0x10013f80, "GetCurrentSampleRate"),
    (0x10014030, "HasSM3"),
    (0x100140c0, "HasSM3_1"),
    (0x10014150, "_AtomicEnterCritical"),
    (0x100141e0, "_AtomicLeaveCritical"),
    (0x1001425c, "OpenMonitorTool"),
    (0x10014594, "CloseMonitorTool"),
    (0x1001465c, "SwitchedOut"),
    (0x100146b0, "SwitchedIn"),
    (0x1001471c, "ColorCheck"),
    (0x1001485c, "SetMonitorClut"),
    (0x10014a34, "FadeToColor"),
    (0x10014cf8, "FadeToClut"),
    (0x10014fe4, "HueScale"),
    (0x10015154, "SetClutToColor"),
    (0x100152f8, "MapToHue"),
    (0x1001556c, "FindMonitor"),
    (0x1001571c, "SwitchDepths"),
    (0x10015934, "TryMainMonitor"),
    (0x10015a98, "GetCurrentClut"),
    (0x10015b44, "SaveCurrentClut"),
    (0x10015c80, "RestoreClut"),
    (0x10015e48, "RestoreOriginalClut"),
    (0x10016004, "ForceScreenUpdate"),
    (0x10016124, "ForceMonitorUpdate"),
    (0x1001620c, "UnprotectMonitor"),
    (0x100162f8, "DepthAvail"),
    (0x10016374, "SubtractFractCluts"),
    (0x10016434, "FractClutToCTab"),
    (0x10016514, "CTabToFractClut"),
    (0x1001666c, "InitOnTheFlyFade"),
    (0x10016918, "ScoreToCode"),
    (0x10016a24, "GenerateChecksum"),
    (0x10016ae4, "GenerateProductCode"),
    (0x10016b98, "ROLlong"),
    (0x10016bec, "InitInputSprocket"),
    (0x10016d94, "InputExit"),
    (0x10016de4, "HideCursorSafe"),
    (0x10016e3c, "ShowCursorSafe"),
    (0x10016e94, "InputActivate"),
    (0x10016f58, "GetInputH"),
    (0x10016fc4, "GetInputV"),
    (0x10017030, "GetInputFire"),
    (0x100170b0, "FastInitialize"),
    (0x10017400, "FastMilliseconds"),
    (0x100175bc, "PollRTC_"),
    (0x10017600, "PollTBR_"),
    (0x10017644, "FindFunctionInSharedLib"),
]

def main():
    fm = currentProgram.getFunctionManager()
    made = renamed = missed = 0
    for value, name in SYMBOLS:
        addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value)
        if currentProgram.getMemory().getBlock(addr) is None:
            missed += 1
            continue
        fn = fm.getFunctionAt(addr)
        if fn is None:
            fn = createFunction(addr, name)
            if fn is not None:
                made += 1
        else:
            renamed += 1
        if fn is not None:
            fn.setName(name, SourceType.USER_DEFINED)
        else:
            createLabel(addr, name, True)
    print("Apeiron: %d functions created, %d renamed, %d addresses not in this program" %
          (made, renamed, missed))
    if missed:
        print("  (if everything was missed, the code section is not at 0x10000000 here - "
              "re-import with Ghidra's PEF loader, or shift the addresses by the difference)")

main()
