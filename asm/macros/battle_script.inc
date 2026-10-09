@ commands
	.macro attackcanceler
	.byte B_SCR_OP_ATTACKCANCELER
	.endm

	.macro printattackstring
	.byte B_SCR_OP_PRINTATTACKSTRING
	.endm

	.macro printselectionstringfromtable ptr:req
	.byte B_SCR_OP_PRINTSELECTIONSTRINGFROMTABLE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro typecalc
	.byte B_SCR_OP_TYPECALC
	.endm

	.macro multihitresultmessage
	.byte B_SCR_OP_MULTIHITRESULTMESSAGE
	.endm

	.macro attackanimation
	.byte B_SCR_OP_ATTACKANIMATION
	.endm

	.macro waitanimation
	.byte B_SCR_OP_WAITANIMATION
	.endm

	.macro healthbarupdate battler:req
	.byte B_SCR_OP_HEALTHBARUPDATE
	.byte \battler
	.endm

	.macro datahpupdate battler:req assuranceDouble:req
	.byte B_SCR_OP_DATAHPUPDATE
	.byte \battler
    .byte \assuranceDouble
	.endm

	.macro critmessage
	.byte B_SCR_OP_CRITMESSAGE
	.endm

	.macro effectivenesssound
	.byte B_SCR_OP_EFFECTIVENESSSOUND
	.endm

	.macro resultmessage
	.byte B_SCR_OP_RESULTMESSAGE
	.endm

	.macro printstring id:req
	.byte B_SCR_OP_PRINTSTRING
	.2byte \id
	.endm

	.macro printsavedstring
	printstring 0
	.endm

	.macro printselectionstring id:req
	.byte B_SCR_OP_PRINTSELECTIONSTRING
	.2byte \id
	.endm

	.macro waitmessage time:req
	.byte B_SCR_OP_WAITMESSAGE
	.2byte \time
	.endm

	.macro printfromtable ptr:req
	.byte B_SCR_OP_PRINTFROMTABLE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro setadditionaleffects
	.byte B_SCR_OP_SETADDITIONALEFFECTS
	.endm

	.macro seteffectprimary battler:req, effectBattler:req, moveEffect=0
	.if \moveEffect != 0
		setmoveeffect \moveEffect
	.endif
	.byte B_SCR_OP_SETEFFECTPRIMARY
	.byte \battler
	.byte \effectBattler
	.endm

	.macro clearvolatile battler:req, volatile:req
	.byte B_SCR_OP_CLEARVOLATILE
	.byte \battler
	.byte \volatile
	.endm

	.macro tryfaintmon battler:req
	.byte B_SCR_OP_TRYFAINTMON
	.byte \battler
	.byte FALSE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte NULL
	.else
	.4byte NULL
	.endif
	.endm

	.macro tryfaintmon_spikes battler:req, instr:req
	.byte B_SCR_OP_TRYFAINTMON
	.byte \battler
	.byte TRUE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \instr
	.else
	.4byte \instr
	.endif
	.endm

	.macro dofaintanimation battler:req
	.byte B_SCR_OP_DOFAINTANIMATION
	.byte \battler
	.endm

	.macro cleareffectsonfaint battler:req
	.byte B_SCR_OP_CLEAREFFECTSONFAINT
	.byte \battler
	.endm

	.macro jumpifstatus battler:req, flags:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFSTATUS
	.byte \battler
	.4byte \flags
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifvolatile battler:req, volatile:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFVOLATILE
	.byte \battler
	.byte \volatile
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifability battler:req, ability:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFABILITY
	.byte \battler
	.2byte \ability
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifsideaffecting battler:req, flags:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFSIDEAFFECTING
	.byte \battler
	.4byte \flags
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifstat battler:req, comparison:req, stat:req, value:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFSTAT
	.byte \battler
	.byte \comparison
	.byte \stat
	.byte \value
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifstatignorecontrary battler:req, comparison:req, stat:req, value:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFSTATIGNORECONTRARY
	.byte \battler
	.byte \comparison
	.byte \stat
	.byte \value
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpbasedontype battler:req, type:req, jumpIfType:req, jumpInstr:req
	.byte B_SCR_OP_JUMPBASEDONTYPE
	.byte \battler
	.byte \type
	.byte \jumpIfType
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpiftype battler:req, type:req, jumpInstr:req
	jumpbasedontype \battler, \type, TRUE, \jumpInstr
	.endm

	.macro jumpifnottype battler:req, type:req, jumpInstr:req
	jumpbasedontype \battler, \type, FALSE, \jumpInstr
	.endm

	.macro getexp battler:req
	.byte B_SCR_OP_GETEXP
	.byte \battler
	.endm

	.macro checkteamslost jumpInstr:req
	.byte B_SCR_OP_CHECKTEAMSLOST
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro goto instr:req
	.byte B_SCR_OP_GOTO
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \instr
	.else
	.4byte \instr
	.endif
	.endm

	.macro jumpifbyte comparison:req, bytePtr:req, value:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFBYTE
	.byte \comparison
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \bytePtr
	.else
	.4byte \bytePtr
	.endif
	.byte \value
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifhalfword comparison:req, halfwordPtr:req, value:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFHALFWORD
	.byte \comparison
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \halfwordPtr
	.else
	.4byte \halfwordPtr
	.endif
	.2byte \value
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifword comparison:req, wordPtr:req, value:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFWORD
	.byte \comparison
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \wordPtr
	.else
	.4byte \wordPtr
	.endif
	.4byte \value
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifarrayequal array1:req, array2:req, size:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFARRAYEQUAL
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \array1
	.else
	.4byte \array1
	.endif
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \array2
	.else
	.4byte \array2
	.endif
	.byte \size
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifarraynotequal array1:req, array2:req, size:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFARRAYNOTEQUAL
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \array1
	.else
	.4byte \array1
	.endif
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \array2
	.else
	.4byte \array2
	.endif
	.byte \size
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro setbyte bytePtr:req, value:req
	.byte B_SCR_OP_SETBYTE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \bytePtr
	.else
	.4byte \bytePtr
	.endif
	.byte \value
	.endm

	.macro addbyte bytePtr:req, value:req
	.byte B_SCR_OP_ADDBYTE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \bytePtr
	.else
	.4byte \bytePtr
	.endif
	.byte \value
	.endm

	.macro subbyte bytePtr:req, value:req
	.byte B_SCR_OP_SUBBYTE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \bytePtr
	.else
	.4byte \bytePtr
	.endif
	.byte \value
	.endm

	.macro copyarray dest:req, src:req, size:req
	.byte B_SCR_OP_COPYARRAY
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \dest
	.else
	.4byte \dest
	.endif
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \src
	.else
	.4byte \src
	.endif
	.byte \size
	.endm

	.macro copyarraywithindex dest:req, src:req, indexPtr:req, size:req
	.byte B_SCR_OP_COPYARRAYWITHINDEX
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \dest
	.else
	.4byte \dest
	.endif
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \src
	.else
	.4byte \src
	.endif
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \indexPtr
	.else
	.4byte \indexPtr
	.endif
	.byte \size
	.endm

	.macro orbyte bytePtr:req, value:req
	.byte B_SCR_OP_ORBYTE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \bytePtr
	.else
	.4byte \bytePtr
	.endif
	.byte \value
	.endm

	.macro orhalfword halfwordPtr:req, value:req
	.byte B_SCR_OP_ORHALFWORD
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \halfwordPtr
	.else
	.4byte \halfwordPtr
	.endif
	.2byte \value
	.endm

	.macro orword wordPtr:req, value:req
	.byte B_SCR_OP_ORWORD
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \wordPtr
	.else
	.4byte \wordPtr
	.endif
	.4byte \value
	.endm

	.macro bicbyte bytePtr:req, value:req
	.byte B_SCR_OP_BICBYTE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \bytePtr
	.else
	.4byte \bytePtr
	.endif
	.byte \value
	.endm

	.macro bichalfword halfwordPtr:req, value:req
	.byte B_SCR_OP_BICHALFWORD
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \halfwordPtr
	.else
	.4byte \halfwordPtr
	.endif
	.2byte \value
	.endm

	.macro bicword wordPtr:req, value:req
	.byte B_SCR_OP_BICWORD
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \wordPtr
	.else
	.4byte \wordPtr
	.endif
	.4byte \value
	.endm

	.macro pause frames:req
	.byte B_SCR_OP_PAUSE
	.2byte \frames
	.endm

	.macro setchargingturn
	.byte B_SCR_OP_SETCHARGINGTURN
	.endm

	.macro waitstate
	.byte B_SCR_OP_WAITSTATE
	.endm

	.macro tryselfconfusiondmgformchange
	.byte B_SCR_OP_TRYSELFCONFUSIONDMGFORMCHANGE
	.endm

	.macro return
	.byte B_SCR_OP_RETURN
	.endm

	.macro end
	.byte B_SCR_OP_END
	.endm

	.macro end3
	.byte B_SCR_OP_END3
	.endm

	.macro call instr:req
	.byte B_SCR_OP_CALL
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \instr
	.else
	.4byte \instr
	.endif
	.endm

	.macro setroost
	.byte B_SCR_OP_SETROOST
	.endm

	.macro jumpifabilitypresent ability:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFABILITYPRESENT
	.2byte \ability
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro endselectionscript
	.byte B_SCR_OP_ENDSELECTIONSCRIPT
	.endm

	.macro playanimation battler:req, animId:req, argPtr=NULL
	.byte B_SCR_OP_PLAYANIMATION
	.byte \battler
	.byte \animId
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \argPtr
	.else
	.4byte \argPtr
	.endif
	.endm

	.macro playanimation_var battler:req, animIdPtr:req, argPtr=NULL
	.byte B_SCR_OP_PLAYANIMATION_VAR
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \animIdPtr
	.else
	.4byte \animIdPtr
	.endif
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \argPtr
	.else
	.4byte \argPtr
	.endif
	.endm

	.macro jumpfifsemiinvulnerable battler:req, state:req, jumpInstr:req
	.byte B_SCR_OP_JUMPFIFSEMIINVULNERABLE
	.byte \battler
	.byte \state
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro trainerslidein position:req
	.byte B_SCR_OP_TRAINERSLIDEIN
	.byte \position
	.endm

	.macro moveend endMode:req, endState:req
	.byte B_SCR_OP_MOVEEND
	.byte \endMode
	.byte \endState
	.endm

	@ Help macros for 5 uses of moveend command

	@ All cases
	.macro moveendall
	setbyte sMOVEEND_STATE, 0
	moveend 0, 0
	.endm

	@ Chosen case
	.macro moveendcase case:req
	setbyte sMOVEEND_STATE, \case
	moveend 1, 0
	.endm

	@ All cases from (inclusive)
	.macro moveendfrom from:req
	setbyte sMOVEEND_STATE, \from
	moveend 0, 0
	.endm

	@ All cases from 0 to (not inclusive)
	.macro moveendto to:req
	setbyte sMOVEEND_STATE, 0
	moveend 2, \to
	.endm

	@ Cases from (inclusive) to (not inclusive)
	.macro moveendfromto from:req, to:req
	setbyte sMOVEEND_STATE, \from
	moveend 2, \to
	.endm

	.macro returnatktoball
	.byte B_SCR_OP_RETURNATKTOBALL
	.endm

	.macro getswitchedmondata battler:req
	.byte B_SCR_OP_GETSWITCHEDMONDATA
	.byte \battler
	.endm

	.macro switchindataupdate battler:req
	.byte B_SCR_OP_SWITCHINDATAUPDATE
	.byte \battler
	.endm

	.macro switchinanim battler:req, dontClearTransform:req, dontClearSubstitute:req
	.byte B_SCR_OP_SWITCHINANIM
	.byte \battler
	.byte \dontClearTransform
	.byte \dontClearSubstitute
	.endm

	.macro jumpifcantswitch battler:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFCANTSWITCH
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro openpartyscreen battler:req, failInstr:req
	.byte B_SCR_OP_OPENPARTYSCREEN
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro switchhandleorder battler:req, state:req
	.byte B_SCR_OP_SWITCHHANDLEORDER
	.byte \battler
	.byte \state
	.endm

	.macro switchineffects battler:req
	.byte B_SCR_OP_SWITCHINEFFECTS
	.byte \battler
	.endm

	.macro switchinevents
	.byte B_SCR_OP_SWITCHINEVENTS
	.endm

	.macro playse song:req
	.byte B_SCR_OP_PLAYSE
	.2byte \song
	.endm

	.macro fanfare song:req
	.byte B_SCR_OP_FANFARE
	.2byte \song
	.endm

	.macro playfaintcry battler:req
	.byte B_SCR_OP_PLAYFAINTCRY
	.byte \battler
	.endm

	.macro endlinkbattle
	.byte B_SCR_OP_ENDLINKBATTLE
	.endm

	.macro returntoball battler:req, changingForm:req
	.byte B_SCR_OP_RETURNTOBALL
	.byte \battler
	.byte \changingForm
	.endm

	.macro handlelearnnewmove learnedMoveInstr:req, nothingToLearnInstr:req, isFirstMove:req
	.byte B_SCR_OP_HANDLELEARNNEWMOVE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \learnedMoveInstr
	.else
	.4byte \learnedMoveInstr
	.endif
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \nothingToLearnInstr
	.else
	.4byte \nothingToLearnInstr
	.endif
	.byte \isFirstMove
	.endm

	.macro yesnoboxlearnmove forgotMoveInstr:req
	.byte B_SCR_OP_YESNOBOXLEARNMOVE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \forgotMoveInstr
	.else
	.4byte \forgotMoveInstr
	.endif
	.endm

	.macro yesnoboxstoplearningmove noInstr:req
	.byte B_SCR_OP_YESNOBOXSTOPLEARNINGMOVE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \noInstr
	.else
	.4byte \noInstr
	.endif
	.endm

	.macro hitanimation battler:req
	.byte B_SCR_OP_HITANIMATION
	.byte \battler
	.endm

	.macro getmoneyreward
	.byte B_SCR_OP_GETMONEYREWARD
	.endm

	.macro updatebattlermoves battler:req
	.byte B_SCR_OP_UPDATEBATTLERMOVES
	.byte \battler
	.endm

	.macro swapattackerwithtarget
	.byte B_SCR_OP_SWAPATTACKERWITHTARGET
	.endm

	.macro incrementgamestat stat:req
	.byte B_SCR_OP_INCREMENTGAMESTAT
	.byte \stat
	.endm

	.macro drawpartystatussummary battler:req
	.byte B_SCR_OP_DRAWPARTYSTATUSSUMMARY
	.byte \battler
	.endm

	.macro hidepartystatussummary battler:req
	.byte B_SCR_OP_HIDEPARTYSTATUSSUMMARY
	.byte \battler
	.endm

	.macro jumptocalledmove notChosenMove:req
	.byte B_SCR_OP_JUMPTOCALLEDMOVE
	.byte \notChosenMove
	.endm

	.macro statusanimation battler:req, status=0, isVolatile=FALSE
	.byte B_SCR_OP_STATUSANIMATION
	.byte \battler
	.4byte \status
	.byte \isVolatile
	.endm

	.macro volatileanimation battler:req, volatile:req
	statusanimation \battler, \volatile, TRUE
	.endm

	.macro futuresighttargetfailure failInstr:req
	.byte B_SCR_OP_FUTURESIGHTTARGETFAILURE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro getpossiblenexttarget jumpInstr:req
	.byte B_SCR_OP_GETPOSSIBLENEXTTARGET
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro yesnobox
	.byte B_SCR_OP_YESNOBOX
	.endm

	.macro cancelallactions
	.byte B_SCR_OP_CANCELALLACTIONS
	.endm

	.macro setgravity failInstr:req
	.byte B_SCR_OP_SETGRAVITY
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro removeitem battler:req
	.byte B_SCR_OP_REMOVEITEM
	.byte \battler
	.endm

	.macro atknameinbuff1
	.byte B_SCR_OP_ATKNAMEINBUFF1
	.endm

	.macro drawlvlupbox
	.byte B_SCR_OP_DRAWLVLUPBOX
	.endm

	.macro resetsentmonsvalue
	.byte B_SCR_OP_RESETSENTMONSVALUE
	.endm

	.macro setatktoplayer0
	.byte B_SCR_OP_SETATKTOPLAYER0
	.endm

	.macro makevisible battler:req
	.byte B_SCR_OP_MAKEVISIBLE
	.byte \battler
	.endm

	.macro recordability battler:req
	.byte B_SCR_OP_RECORDABILITY
	.byte \battler
	.endm

	.macro buffermovetolearn
	.byte B_SCR_OP_BUFFERMOVETOLEARN
	.endm

	.macro jumpifplayerran jumpInstr:req
	.byte B_SCR_OP_JUMPIFPLAYERRAN
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro hpthresholds battler:req
	.byte B_SCR_OP_HPTHRESHOLDS
	.byte \battler
	.endm

	.macro hpthresholds2 battler:req
	.byte B_SCR_OP_HPTHRESHOLDS2
	.byte \battler
	.endm

	.macro useitemonopponent
	.byte B_SCR_OP_USEITEMONOPPONENT
	.endm

	.macro setprotectlike
	.byte B_SCR_OP_SETPROTECTLIKE
	.endm

	.macro tryexplosion
	.byte B_SCR_OP_TRYEXPLOSION
	.endm

	.macro setatkhptozero
	.byte B_SCR_OP_SETATKHPTOZERO
	.endm

	.macro jumpifnexttargetvalid jumpInstr:req
	.byte B_SCR_OP_JUMPIFNEXTTARGETVALID
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro tryhealhalfhealth battler:req, failInstr:req
	.byte B_SCR_OP_TRYHEALHALFHEALTH
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setfieldweather
	.byte B_SCR_OP_SETFIELDWEATHER
	.endm

	.macro setreflect
	.byte B_SCR_OP_SETREFLECT
	.endm

	.macro setseeded
	.byte B_SCR_OP_SETSEEDED
	.endm

	.macro manipulatedamage mode:req
	.byte B_SCR_OP_MANIPULATEDAMAGE
	.byte \mode
	.endm

	.macro trysetrest
	.byte B_SCR_OP_TRYSETREST
	.endm

	.macro jumpifuproarwakes jumpInstr:req
	.byte B_SCR_OP_JUMPIFUPROARWAKES
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro stockpiletohpheal failInstr:req
	.byte B_SCR_OP_STOCKPILETOHPHEAL
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro normalisebuffs
	.byte B_SCR_OP_NORMALISEBUFFS
	.endm

	.macro twoturnmoveschargestringandanimation
	.byte B_SCR_OP_TWOTURNMOVESCHARGESTRINGANDANIMATION
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte 1f @animation then attack string
	.else
	.4byte 1f @animation then attack string
	.endif
	@default - attack string then animation
	printsavedstring
	waitmessage B_WAIT_TIME_LONG
	attackanimation
	waitanimation
	goto 2f
	1:
	attackanimation
	waitanimation
	printsavedstring
	waitmessage B_WAIT_TIME_LONG
	2:
	.endm

	.macro trynonvolatilestatus
	.byte B_SCR_OP_TRYNONVOLATILESTATUS
	.endm

	.macro initmultihitstring
	.byte B_SCR_OP_INITMULTIHITSTRING
	.endm

	.macro forcerandomswitch failInstr:req
	.byte B_SCR_OP_FORCERANDOMSWITCH
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro tryconversiontypechange failInstr:req
	.byte B_SCR_OP_TRYCONVERSIONTYPECHANGE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro givepaydaymoney
	.byte B_SCR_OP_GIVEPAYDAYMONEY
	.endm

	.macro setlightscreen
	.byte B_SCR_OP_SETLIGHTSCREEN
	.endm

	.macro tryKO failInstr:req
	.byte B_SCR_OP_TRYKO
	.4byte \failInstr
	.endm

	.macro checknonvolatiletrigger nonVolatile:req, failInstr:req
	.byte B_SCR_OP_CHECKNONVOLATILETRIGGER
	.2byte \nonVolatile
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro animatewildpokemonafterfailedpokeball battler:req
	.byte B_SCR_OP_ANIMATEWILDPOKEMONAFTERFAILEDPOKEBALL
	.byte \battler
	.endm

	.macro tryinfatuating failInstr:req
	.byte B_SCR_OP_TRYINFATUATING
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro updatestatusicon battler:req
	.byte B_SCR_OP_UPDATESTATUSICON
	.byte \battler
	.endm

	.macro setmist
	.byte B_SCR_OP_SETMIST
	.endm

	.macro setfocusenergy battler:req
	.byte B_SCR_OP_SETFOCUSENERGY
	.byte \battler
	.endm

	.macro transformdataexecution
	.byte B_SCR_OP_TRANSFORMDATAEXECUTION
	.endm

	.macro setsubstitute
	.byte B_SCR_OP_SETSUBSTITUTE
	.endm

	.macro mimicattackcopy failInstr:req
	.byte B_SCR_OP_MIMICATTACKCOPY
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setcalledmove
	.byte B_SCR_OP_SETCALLEDMOVE
	.endm

	.macro disablelastusedattack failInstr:req
	.byte B_SCR_OP_DISABLELASTUSEDATTACK
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trysetencore failInstr:req
	.byte B_SCR_OP_TRYSETENCORE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro painsplitdmgcalc failInstr:req
	.byte B_SCR_OP_PAINSPLITDMGCALC
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro settypetorandomresistance failInstr:req
	.byte B_SCR_OP_SETTYPETORANDOMRESISTANCE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setalwayshitflag
	.byte B_SCR_OP_SETALWAYSHITFLAG
	.endm

	.macro copymovepermanently failInstr:req
	.byte B_SCR_OP_COPYMOVEPERMANENTLY
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro settailwind failInstr:req
	.byte B_SCR_OP_SETTAILWIND
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro tryspiteppreduce failInstr:req
	.byte B_SCR_OP_TRYSPITEPPREDUCE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro healpartystatus
	.byte B_SCR_OP_HEALPARTYSTATUS
	.endm

	.macro cursetarget failInstr:req
	.byte B_SCR_OP_CURSETARGET
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trysetspikes failInstr:req
	.byte B_SCR_OP_TRYSETSPIKES
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setvolatile battler:req, volatile:req, value=TRUE
	.byte B_SCR_OP_SETVOLATILE
	.byte \battler
	.byte \volatile
	.byte \value
	.endm

	.macro trysetperishsong failInstr:req
	.byte B_SCR_OP_TRYSETPERISHSONG
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro jumpifconfusedandstatmaxed stat:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFCONFUSEDANDSTATMAXED
	.byte \stat
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro setembargo failInstr:req
	.byte B_SCR_OP_SETEMBARGO
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setsafeguard
	.byte B_SCR_OP_SETSAFEGUARD
	.endm

	.macro jumpifnopursuitswitchdmg jumpInstr:req
	.byte B_SCR_OP_JUMPIFNOPURSUITSWITCHDMG
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro tryactivateitem battler:req, flag:req
	.byte B_SCR_OP_TRYACTIVATEITEM
	.byte \battler
	.byte \flag
	.endm

	.macro copyfoestats
	.byte B_SCR_OP_COPYFOESTATS
	.endm

	.macro rapidspinfree
	.byte B_SCR_OP_RAPIDSPINFREE
	.endm

	.macro recoverbasedonsunlight failInstr:req
	.byte B_SCR_OP_RECOVERBASEDONSUNLIGHT
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setstickyweb failInstr:req
	.byte B_SCR_OP_SETSTICKYWEB
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro selectfirstvalidtarget
	.byte B_SCR_OP_SELECTFIRSTVALIDTARGET
	.endm

	.macro setsemiinvulnerablebit clear=FALSE
	.byte B_SCR_OP_SETSEMIINVULNERABLEBIT
	.byte \clear
	.endm

	.macro clearsemiinvulnerablebit
	setsemiinvulnerablebit TRUE
	.endm

	.macro trymemento failInstr:req
	.byte B_SCR_OP_TRYMEMENTO
	.4byte \failInstr
	.endm

	.macro setforcedtarget
	.byte B_SCR_OP_SETFORCEDTARGET
	.endm

	.macro curestatuswithmove failInstr:req
	.byte B_SCR_OP_CURESTATUSWITHMOVE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro settorment failInstr:req
	.byte B_SCR_OP_SETTORMENT
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro settaunt failInstr:req
	.byte B_SCR_OP_SETTAUNT
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trysethelpinghand failInstr:req
	.byte B_SCR_OP_TRYSETHELPINGHAND
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro tryswapitems failInstr:req
	.byte B_SCR_OP_TRYSWAPITEMS
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trycopyability battler:req, failInstr:req
	.byte B_SCR_OP_TRYCOPYABILITY
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trywish failInstr:req
	.byte B_SCR_OP_TRYWISH
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro settoxicspikes failInstr:req
	.byte B_SCR_OP_SETTOXICSPIKES
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setgastroacid failInstr:req
	.byte B_SCR_OP_SETGASTROACID
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setyawn failInstr:req
	.byte B_SCR_OP_SETYAWN
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setroom
	.byte B_SCR_OP_SETROOM
	.endm

	.macro tryswapabilities failInstr:req
	.byte B_SCR_OP_TRYSWAPABILITIES
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro tryimprison failInstr:req
	.byte B_SCR_OP_TRYIMPRISON
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setstealthrock failInstr:req
	.byte B_SCR_OP_SETSTEALTHROCK
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trysetvolatile battler:req, _volatile:req, failInstr:req
	.byte B_SCR_OP_TRYSETVOLATILE
	.byte \battler
	.byte \_volatile
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trysetmagiccoat failInstr:req
	.byte B_SCR_OP_TRYSETMAGICCOAT
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trysetsnatch failInstr:req
	.byte B_SCR_OP_TRYSETSNATCH
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro switchoutabilities battler:req
	.byte B_SCR_OP_SWITCHOUTABILITIES
	.byte \battler
	.endm

	.macro jumpifhasnohp battler:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFHASNOHP
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro pickup
	.byte B_SCR_OP_PICKUP
	.endm

	.macro settypebasedhalvers failInstr:req
	.byte B_SCR_OP_SETTYPEBASEDHALVERS
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro tryrecycleitem failInstr:req
	.byte B_SCR_OP_TRYRECYCLEITEM
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro settypetoenvironment failInstr:req
	.byte B_SCR_OP_SETTYPETOENVIRONMENT
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro snatchsetbattlers
	.byte B_SCR_OP_SNATCHSETBATTLERS
	.endm

	.macro handleballthrow
	.byte B_SCR_OP_HANDLEBALLTHROW
	.endm

	.macro givecaughtmon passInstr:req
	.byte B_SCR_OP_GIVECAUGHTMON
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \passInstr
	.else
	.4byte \passInstr
	.endif
	.endm

	.macro trysetcaughtmondexflags failInstr:req
	.byte B_SCR_OP_TRYSETCAUGHTMONDEXFLAGS
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro displaydexinfo
	.byte B_SCR_OP_DISPLAYDEXINFO
	.endm

	.macro trygivecaughtmonnick
	.byte B_SCR_OP_TRYGIVECAUGHTMONNICK
	.endm

	.macro sortbattlers
	.byte B_SCR_OP_SORTBATTLERS
	.endm

	.macro removeattackerstatus1
	.byte B_SCR_OP_REMOVEATTACKERSTATUS1
	.endm

	.macro finishaction
	.byte B_SCR_OP_FINISHACTION
	.endm

	.macro finishturn
	.byte B_SCR_OP_FINISHTURN
	.endm

	.macro trainerslideout position:req
	.byte B_SCR_OP_TRAINERSLIDEOUT
	.byte \position
	.endm

	.macro settelekinesis failInstr:req
	.byte B_SCR_OP_SETTELEKINESIS
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro swapstatstages stat:req
	.byte B_SCR_OP_SWAPSTATSTAGES
	.byte \stat
	.endm

	.macro averagestats stat:req
	.byte B_SCR_OP_AVERAGESTATS
	.byte \stat
	.endm

	.macro setnonvolatilestatus trigger:req
	.byte B_SCR_OP_SETNONVOLATILESTATUS
	.byte \trigger
	.endm

	.macro tryoverwriteability failInstr:req
	.byte B_SCR_OP_TRYOVERWRITEABILITY
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trysynchronize
	.byte B_SCR_OP_TRY_SYNCHRONIZE
	.endm

	.macro tryconfusionafterskydrop battler:req
	.byte B_SCR_OP_TRY_CONFUSION_AFTER_SKY_DROP
    .byte \battler
	.endm

    .macro trymovestatchanges
    .byte B_SCR_OP_TRYMOVESTATCHANGES
    .endm

    .macro trystatchanges battler:req statChangeFlags:req
    .byte B_SCR_OP_TRYSTATCHANGES
    .byte \battler
    .2byte \statChangeFlags
    .endm

    .macro trybattlerstatchange battler:req statChangeFlags:req
    .byte B_SCR_OP_TRYBATTLERSTATCHANGE
    .byte \battler
    .2byte \statChangeFlags
    .endm

	.macro callnative func:req
	.byte B_SCR_OP_CALLNATIVE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \func
	.else
	.4byte \func
	.endif
	.endm

	.macro savetarget
	callnative BS_SaveTarget
	.endm

	.macro restoretarget
	callnative BS_RestoreTarget
	.endm

	.macro saveattacker
	callnative BS_SaveAttacker
	.endm

	.macro restoreattacker
	callnative BS_RestoreAttacker
	.endm

	.macro allyswitchswapbattlers
	callnative BS_AllySwitchSwapBattler
	.endm

	.macro tryallyswitch jumpInstr:req
	callnative BS_TryAllySwitch
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifmorethanhalfHP battler:req, jumpInstr:req
	callnative BS_JumpIfMoreThanHalfHP
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro itemrestorehp jumpInstr:req, restoreBattlerInstr:req
	callnative BS_ItemRestoreHP
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \restoreBattlerInstr
	.else
	.4byte \restoreBattlerInstr
	.endif
	.endm

	.macro itemcurestatus jumpInstr:req, restoreBattlerInstr:req
	callnative BS_ItemCureStatus
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \restoreBattlerInstr
	.else
	.4byte \restoreBattlerInstr
	.endif
	.endm

	.macro itemincreasestat
	callnative BS_ItemIncreaseStat
	.endm

	.macro itemrestorepp
	callnative BS_ItemRestorePP
	.endm

	.macro tryrevertweatherform
	callnative BS_TryRevertWeatherForm
	.endm

	.macro trysetoctolock failInstr:req
	callnative BS_TrySetOctolock
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setzeffect
	callnative BS_SetZEffect
	.endm

	@ Used by effects that may proc Symbiosis but do not call removeitem.
	.macro trysymbiosis battler:req
	callnative BS_TrySymbiosis
	.byte \battler
	.endm

	@ returns B_SIDE_x to gBattleCommunication[0]
	.macro getbattlerside battler:req
	callnative BS_GetBattlerSide
	.byte \battler
	.endm

	.macro checkparentalbondcounter counter:req, ptr:req
	callnative BS_CheckParentalBondCounter
	.byte \counter
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro jumpifcantloseitem ptr:req
	callnative BS_JumpIfCantLoseItem
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro setterrain failInstr:req
	callnative BS_SetTerrain
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro jumpifterrainaffected battler:req, terrainFlags:req, jumpInstr:req
	callnative BS_JumpIfTerrainAffected
	.byte \battler
	.byte \terrainFlags
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro tryreflecttype failInstr:req
	callnative BS_TryReflectType
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	@ Used to active a different Max Move effects.
	.macro setmaxmoveeffect
	callnative BS_SetMaxMoveEffect
	.endm

	.macro setsteelsurge, failInstr:req
	callnative BS_SetSteelsurge
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trysetparalysis, ptr:req
	callnative BS_TrySetParalysis
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro trysetpoison, ptr:req
	callnative BS_TrySetPoison
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro trysetpoisonparalysis, ptr:req
	callnative BS_TrySetPoisonParalyzis
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro tryseteffectspore, ptr:req
	callnative BS_TrySetEffectSpore
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro trysetconfusion, ptr:req
	callnative BS_TrySetConfusion
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro trysetinfatuation, ptr:req
	callnative BS_TrySetInfatuation
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro trysetescapeprevention, ptr:req
	callnative BS_TrySetEscapePrevention
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro trysettorment, ptr:req
	callnative BS_TrySetTorment
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro tryhealsixthhealth, ptr:req
	callnative BS_HealOneSixth
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro tryrecycleberry, ptr:req
	callnative BS_TryRecycleBerry
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro updatedynamax
	callnative BS_UpdateDynamax
	.endm

	.macro jumpiftargetdynamaxed, ptr:req
	callnative BS_JumpIfDynamaxed
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \ptr
	.else
	.4byte \ptr
	.endif
	.endm

	.macro undodynamax battler:req
	callnative BS_UndoDynamax
	.byte \battler
	.endm

	.macro trytrainerslidezmovemsg
	callnative BS_TryTrainerSlideZMoveMsg
	.endm

	.macro trytrainerslidemegaevolutionmsg
	callnative BS_TryTrainerSlideMegaEvolutionMsg
	.endm

	.macro trytrainerslidedynamaxmsg
	callnative BS_TryTrainerSlideDynamaxMsg
	.endm

	.macro trytrainerslideteramsg
	callnative BS_TryTrainerSlideTeraMsg
	.endm

	.macro tryhealpulse failInstr:req
	callnative BS_TryHealPulse
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trydefog clear:req, failInstr:req
	callnative BS_TryDefog
	.byte \clear
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trytriggerstatusform
	callnative BS_TryTriggerStatusForm
	.endm

	.macro tryupperhand failInstr:req
	callnative BS_TryUpperHand
	.4byte \failInstr
	.endm

	.macro trytidyup clear:req, jumpInstr:req
	callnative BS_TryTidyUp
	.byte \clear
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro tryquash failInstr:req
	callnative BS_TryQuash
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro removeweather
	callnative BS_RemoveWeather
	.endm

	.macro applyterastallization
	callnative BS_ApplyTerastallization
	.endm

	.macro jumpifsleepclause jumpInstr:req
	callnative BS_JumpIfSleepClause
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifnoberry battler:req, jumpInstr:req
	callnative BS_JumpIfNoBerry
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro checkteatimetargets failInstr:req
	callnative BS_CheckTeaTimeTargets
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trywindriderpower battler:req, failInstr:req
	callnative BS_TryWindRiderPower
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro activateweatherchangeabilities battler:req
	callnative BS_ActivateWeatherChangeAbilities
	.byte \battler
	.endm

	.macro activateterrainchangeabilities battler:req
	callnative BS_ActivateTerrainChangeAbilities
	.byte \battler
	.endm

	.macro resetterrainabilityflags
	callnative BS_ResetTerrainAbilityFlags
	.endm

	@ Stores Healing Wish effect.
	.macro storehealingwish battler:req
	callnative BS_StoreHealingWish
	.byte \battler
	.endm

	.macro tryrevivalblessing, failInstr:req
	callnative BS_TryRevivalBlessing
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro jumpifblockedbysoundproof battler:req, failInstr:req
	callnative BS_JumpIfBlockedBySoundproof
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro jumpifcommanderactive battler:req, jumpInstr:req
	callnative BS_JumpIfCommanderActive
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro checkpokeflute
	callnative BS_CheckPokeFlute
	.endm

	.macro waitfanfare
	callnative BS_WaitFanfare
	.endm

	.macro removeterrain
	callnative BS_RemoveTerrain
	.endm

	.macro setmoveresultflags flags:req
	callnative BS_SetMoveResultFlags
	.4byte \flags
	.endm

	.macro clearspecialstatuses
	callnative BS_ClearSpecialStatuses
	.endm

	.macro clearmoveresultflags flags:req
	callnative BS_ClearMoveResultFlags
	.4byte \flags
	.endm

	.macro jumpifmoveresultflags flags:req failInstr:req
	callnative BS_JumpIfMoveResultFlags
	.4byte \flags
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro jumpifnotcriticalhit failInstr:req
	callnative BS_JumpIfNotCriticalHit
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro jumpifcangigantamax battler:req, jumpInstr:req
	callnative BS_JumpIfCanGigantamax
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro swapsidestatuses
	callnative BS_CourtChangeSwapSideStatuses
	.endm

	.macro swapstats stat:req
	callnative BS_SwapStats
	.byte \stat
	.endm

	.macro restoresavedmove
	callnative BS_RestoreSavedMove
	.endm

    .macro trydefiantrattled
    callnative BS_TryDefiantRattled
    .endm

    .macro tryadrenalineorb
    callnative BS_TryAdrenalineOrb
    .endm

    .macro raisecritstatchangeanim battler:req
    callnative BS_RaiseCritStatChangeAnim
    .byte \battler
    .endm

    .macro restorestatchangequeue
    callnative BS_RestoreStatChangeQueue
    .endm

@ helpful macros
	.macro setmoveeffect effect:req
	sethword sMOVE_EFFECT, \effect
	.endm

	.macro sethword dst:req, value:req
	setbyte \dst, (\value) & 0xFF
	setbyte \dst + 1, ((\value) >> 8) & 0xFF
	.endm

	.macro setword dst:req, value:req
	setbyte \dst, (\value) & 0xFF
	setbyte \dst + 1, ((\value) >> 8) & 0xFF
	setbyte \dst + 2, ((\value) >> 16) & 0xFF
	setbyte \dst + 3, ((\value) >> 24) & 0xFF
	.endm

	.macro copybyte dst:req, src:req
	copyarray \dst, \src, 0x1
	.endm

	.macro copyhword dst:req, src:req
	copyarray \dst, \src, 0x2
	.endm

	.macro copyword dst:req, src:req
	copyarray \dst, \src, 0x4
	.endm

	.macro jumpifbytenotequal byte1:req, byte2:req, jumpInstr:req
	jumpifarraynotequal \byte1, \byte2, 0x1, \jumpInstr
	.endm

	.macro jumpifbyteequal byte1:req, byte2:req, jumpInstr:req
	jumpifarrayequal \byte1, \byte2, 0x1, \jumpInstr
	.endm

	.macro jumpifmove move:req, jumpInstr:req
	jumpifhalfword CMP_EQUAL, gCurrentMove, \move, \jumpInstr
	.endm

	.macro jumpifnotmove move:req, jumpInstr:req
	jumpifhalfword CMP_NOT_EQUAL, gCurrentMove, \move, \jumpInstr
	.endm

	.macro jumpifnotchosenmove move:req, jumpInstr:req
	jumpifhalfword CMP_NOT_EQUAL, gChosenMove, \move, \jumpInstr
	.endm

	.macro jumpifmovehadnoeffect jumpInstr:req
	jumpifmoveresultflags MOVE_RESULT_NO_EFFECT, \jumpInstr
	.endm

	.macro jumpifside battler:req, side:req, equalJumpInstr:req
	getbattlerside \battler
	jumpifbyte CMP_EQUAL, gBattleCommunication, \side, \equalJumpInstr
	.endm

	.macro jumpifbattletype flags:req, jumpInstr:req
	jumpifword CMP_COMMON_BITS, gBattleTypeFlags, \flags, \jumpInstr
	.endm

	.macro jumpifnotbattletype flags:req, jumpInstr:req
	jumpifword CMP_NO_COMMON_BITS, gBattleTypeFlags, \flags, \jumpInstr
	.endm

	.macro dmg_1_8_targethp
	manipulatedamage DMG_1_8_TARGET_HP
	.endm

	.macro jumpifflowerveil jumpInstr:req
	jumpifnottype BS_TARGET, TYPE_GRASS, 1f
	jumpifability BS_TARGET_SIDE, ABILITY_FLOWER_VEIL, \jumpInstr
	1:
	.endm

	.macro jumpifflowerveilattacker jumpInstr:req
	jumpifnottype BS_ATTACKER, TYPE_GRASS, 1f
	jumpifability BS_ATTACKER_SIDE, ABILITY_FLOWER_VEIL, \jumpInstr
	1:
	.endm

	.macro jumpifabilitycantbereactivated battler:req, jumpInstr:req
	callnative BS_JumpIfAbilityCantBeReactivated
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro tryactivateabilityshield battler:req
	callnative BS_TryActivateAbilityShield
	.byte \battler
	.endm

	.macro jumpifroarfails jumpInstr:req
	callnative BS_JumpIfRoarFails
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifabsent battler:req, jumpInstr:req
	callnative BS_JumpIfAbsent
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifnoholdeffect battler:req, holdEffect:req, jumpInstr:req
	jumpifholdeffect \battler, \holdEffect, \jumpInstr, FALSE
	.endm

	.macro jumpifholdeffect battler:req, holdEffect:req, jumpInstr:req, equal:req
	callnative BS_JumpIfHoldEffect
	.byte \battler
	.byte \holdEffect
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.byte \equal
	.endm

	.macro jumpifnoally battler:req, jumpInstr:req
	callnative BS_JumpIfNoAlly
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro setlastuseditem battler:req
	callnative BS_SetLastUsedItem
	.byte \battler
	.endm

	.macro trysetfairylock failInstr:req
	callnative BS_TrySetFairyLock
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro jumpiffullhp battler:req, jumpInstr:req
	callnative BS_JumpIfFullHp
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro tryfriskmessage
	callnative BS_TryFriskMessage
	.endm

	.macro settracedability battler:req
	callnative BS_SetTracedAbility
	.byte \battler
	.endm

	.macro tryillusionoff battler:req
	callnative BS_TryIllusionOff
	.byte \battler
	.endm

	.macro updatenick
	callnative BS_UpdateNick
	.endm

	.macro gravityonairbornemons
	callnative BS_GravityOnAirborneMons
	.endm

	.macro cancelmultiturnmoves
	callnative BS_CancelMultiTurnMoves
	.endm

	.macro isrunningimpossible
	callnative BS_IsRunningImpossible
	.endm

	.macro getmovetarget
	callnative BS_GetMoveTarget
	.endm

	@ Will jump to script pointer if the specified battler has or has not fainted.
	.macro jumpiffainted battler:req, value:req, ptr:req
	getbattlerfainted \battler
	jumpifbyte CMP_EQUAL, gBattleCommunication, \value, \ptr
	.endm

	.macro getbattlerfainted battler:req
	callnative BS_GetBattlerFainted
	.byte \battler
	.endm

	.macro resetswitchinabilitybits
	callnative BS_ResetSwitchInAbilityBits
	.endm

	.macro updatechoicemoveonlvlup
	callnative BS_UpdateChoiceMoveOnLvlUp
	.endm

	.macro resetplayerfainted
	callnative BS_ResetPlayerFainted
	.endm

	.macro palaceflavortext
	callnative BS_PalaceFlavorText
	.endm

	.macro arenajudgmentwindow
	callnative BS_ArenaJudgmentWindow
	.endm

	.macro arenaopponentmonlost
	callnative BS_ArenaOpponentMonLost
	.endm

	.macro arenaplayermonlost
	callnative BS_ArenaPlayerMonLost
	.endm

	.macro arenabothmonslost
	callnative BS_ArenaBothMonsLost
	.endm

	.macro forfeityesnobox
	callnative BS_ForfeitYesNoBox
	.endm

	.macro drawarenareftextbox
	callnative BS_DrawArenaRefTextBox
	.endm

	.macro erasearenareftextbox
	callnative BS_EraseArenaRefTextBox
	.endm

	.macro arenajudgmentstring id:req
	callnative BS_ArenaJudgmentString
	.byte \id
	.endm

	.macro arenawaitmessage
	callnative BS_ArenaWaitMessage
	.endm

	.macro waitcry
	callnative BS_WaitCry
	.endm

	.macro returnopponentmon1toball
	callnative BS_ReturnOpponentMon1ToBall
	.endm

	.macro returnopponentmon2toball
	callnative BS_ReturnOpponentMon2ToBall
	.endm

	.macro volumedown
	callnative BS_VolumeDown
	.endm

	.macro volumeup
	callnative BS_VolumeUp
	.endm

	.macro setalreadystatusedmoveattempt
	callnative BS_SetAlreadyStatusedMoveAttempt
	.endm

	.macro palacetryescapestatus
	callnative BS_PalaceTryEscapeStatus
	.endm

	.macro setteleportoutcome battler:req
	callnative BS_SetTeleportOutcome
	.byte \battler
	.endm

	.macro playtrainerdefeatedmusic
	callnative BS_PlayTrainerDefeatedMusic
	.endm

	.macro stattextbuffer
	callnative BS_StatTextBuffer
	.endm

	.macro switchinabilities battler:req
	callnative BS_SwitchinAbilities
	.byte \battler
	.endm

	.macro effectsafterformchange
	callnative BS_EffectsAfterFormChange
	.endm

	.macro abilityonformchange battler:req
	callnative BS_AbilityOnFormChange
	.byte \battler
	.endm

	.macro instanthpdrop
	callnative BS_InstantHpDrop
	.endm

	.macro clearstatus battler:req
	callnative BS_ClearStatus
	.byte \battler
	.endm

	.macro restoremovepp battler:req
	callnative BS_RestoreMovePp
	.byte \battler
	.endm

	.macro tryactivatereceiver battler:req
	callnative BS_TryActivateReceiver
	.byte \battler
	.endm

	.macro tryactivatesoulheart
	callnative BS_TryActivateSoulheart
	.endm

	.macro playmoveanimation move:req
	callnative BS_PlayMoveAnimation
	.2byte \move
	.endm

	.macro setluckychant failInstr:req
	callnative BS_SetLuckyChant
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro tryentrainment failInstr:req
	callnative BS_TryEntrainment
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro setlastusedability
	callnative BS_SetLastUsedAbility
	.endm

	.macro invertstatstages
	callnative BS_InvertStatStages
	.endm

	.macro tryelectrify failInstr:req
	callnative BS_TryElectrify
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trysoak failInstr:req
	callnative BS_TrySoak
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro handleformchange battler:req, caseId:req, bufferSpeciesName=TRUE
	callnative BS_HandleFormChange
	.byte \battler
	.byte \caseId
	.byte \bufferSpeciesName
	.endm

	.macro tryinstruct failInstr:req
	callnative BS_TryInstruct
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro showabilitypopup
	callnative BS_ShowAbilityPopup
	.endm

	.macro updateabilitypopup
	callnative BS_UpdateAbilityPopup
	.endm

	.macro waitabilitypopup
	callnative BS_WaitAbilityPopup
	.endm

	.macro jumpiftargetally jumpInstr:req
	callnative BS_JumpIfTargetAlly
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro trypsychoshift failInstr:req sleepClauseFailInstr:req
	callnative BS_TryPsychoShift
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \sleepClauseFailInstr
	.else
	.4byte \sleepClauseFailInstr
	.endif
	.endm

	.macro curestatus battler:req
	callnative BS_CureStatus
	.byte \battler
	.endm

	.macro powertrick
	callnative BS_PowerTrick
	.endm

	.macro tryafteryou failInstr:req
	callnative BS_TryAfterYou
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro trybestow failInstr:req
	callnative BS_TryBestow
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro handletrainerslidemsg battler:req, case_:req
	callnative BS_HandleTrainerSlideMsg
	.byte \battler
	.byte \case_
	.endm

	.macro trytrainerslidemsgfirstoff battler:req
	callnative BS_TryTrainerSlideMsgFirstOff
	.byte \battler
	.endm

	.macro trytrainerslidemsglaston battler:req
	callnative BS_TryTrainerSlideMsgLastOn
	.byte \battler
	.endm

	.macro setauroraveil
	callnative BS_SetAuroraVeil
	.endm

	.macro trythirdtype failInstr:req
	callnative BS_TryThirdType
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro destroyabilitypopup
	callnative BS_DestroyAbilityPopup
	.endm

	.macro activateitemeffects
	callnative BS_ActivateItemEffects
	.endm

	.macro tryroomservice battler:req, failInstr:req
	callnative BS_TryRoomService
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro tryterrainseed battler:req, failInstr:req
	callnative BS_TryTerrainSeed
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro makeinvisible battler:req
	callnative BS_MakeInvisible
	.byte \battler
	.endm

	.macro jumpifteamhealthy jumpInstr:req
	callnative BS_JumpIfTeamHealthy
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro tryhealquarterhealth battler:req, failInstr:req
	callnative BS_TryHealQuarterHealth
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro jumpifpranksterblocked jumpInstr:req
	callnative BS_JumpIfPranksterBlocked
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro trytoclearprimalweather
	callnative BS_TryToClearPrimalWeather
	.endm

	.macro tryendneutralizinggas battler=BS_TARGET
	callnative BS_TryEndNeutralizingGas
	.byte \battler
	.endm

	.macro consumeberry battler:req, fromBattler:req
	callnative BS_ConsumeBerry
	.byte \battler
	.byte \fromBattler
	.endm

	.macro jumpifweatheraffected flags:req, jumpInstr:req
	callnative BS_JumpIfWeatherAffected
	.2byte \flags
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifspecies species:req, jumpInstr:req
	callnative BS_JumpIfSpecies
	.2byte \species
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro jumpifabilitypreventsrest battler:req, jumpInstr:req
	callnative BS_JumpIfAbilityPreventsRest
	.byte \battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro cutonethirdhpandraisestats failInstr:req
	callnative BS_CutOneThirdHpAndRaiseStats
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \failInstr
	.else
	.4byte \failInstr
	.endif
	.endm

	.macro tryresetnegativestatstages
	callnative BS_TryResetNegativeStatStages
	.endm

	.macro savebattleritem
	callnative BS_SaveBattlerItem
	.endm

	.macro restorebattleritem
	callnative BS_RestoreBattlerItem
	.endm

	.macro battleritemtolastuseditem
	callnative BS_BattlerItemToLastUsedItem
	.endm

	.macro setallytonexttarget jumpInstr:req
	jumpifbyte CMP_GREATER_THAN, gBattlerTarget, 0x1, 1f
	addbyte gBattlerTarget, 0x2
	goto \jumpInstr
	1:
	subbyte gBattlerTarget, 0x2
	goto \jumpInstr
	.endm

	.macro setallytonextattacker jumpInstr:req
	jumpifbyte CMP_GREATER_THAN, gBattlerAttacker, 0x1, 1f
	addbyte gBattlerAttacker, 0x2
	goto \jumpInstr
	1:
	subbyte gBattlerAttacker, 0x2
	goto \jumpInstr
	.endm

	.macro jumpifsafeguard jumpInstr:req
	jumpiftargetally 2f
	jumpifability BS_ATTACKER, ABILITY_INFILTRATOR, 1f
	2:
	jumpifsideaffecting BS_TARGET, SIDE_STATUS_SAFEGUARD, \jumpInstr
	1:
	.endm

	.macro flushtextbox
	printstring STRINGID_EMPTYSTRING3
	waitmessage 1
	.endm

	.macro jumpifnowhiteout jumpInstr:req
	callnative BS_JumpIfNoWhiteOut
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro tryboosterenergy onFieldStatus:req
	callnative BS_TryBoosterEnergy
	.byte \onFieldStatus
	.endm

	.macro jumpifgenconfiglowerthan tag:req, gen:req, jumpInstr:req
	callnative BS_JumpIfGenConfigLowerThan
	.2byte \tag
	.byte \gen
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro tryactivateabilitywithabilityshield battler:req, switchedItems=FALSE
	callnative BS_TryActivateAbilityWithAbilityShield
	.byte \battler
	.byte \switchedItems
	.endm

	.macro endturnevents
	callnative BS_EndTurnEvents
	.endm

	.macro trywakebattlersuproar
	callnative BS_TryWakeBattlersUproar
	.endm

	.macro catchaftervictory
	callnative BS_CatchAfterVictory
	.endm

	.macro jumpifnoballs jumpInstr:req
	callnative BS_JumpIfNoBalls
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro handlefailedvictorycatch
	callnative BS_HandleFailedVictoryCatch
	.endm

	.macro catchornot
	callnative BS_CatchOrNot
	.endm

	.macro showitempopup battler:req
	callnative BS_ShowItemPopup
	.byte \battler
	.endm

	.macro destroyitempopup
	callnative BS_DestroyItemPopup
	.endm

	.macro multihitplurality
	callnative BS_MultiHitPlurality
	.endm

	.macro jumpifterrain comparison:req, terrain:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFTERRAIN
	.byte \comparison
	.byte \terrain
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \jumpInstr
	.else
	.4byte \jumpInstr
	.endif
	.endm

	.macro trydomoveeffectsbeforemoves
	callnative BS_TryDoMoveEffectsBeforeMoves
	.endm
