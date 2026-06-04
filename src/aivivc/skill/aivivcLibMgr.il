; AIVIVC Library Manager menu customization.
; This file is intended for Library Manager context. It loads aivivcCallbacks.il
; in the same context before creating menu items so callback symbols are visible
; to lmgr callbacks.

AiviVCLibMgrRegistered = nil
AiviVCLibMgrMenuCreated = nil
AiviVCLibMgrRegisterRetries = 0
AiviVCLibMgrRegisterWarned = nil

procedure(AiviVCLibMgrNonEmpty(value)
  stringp(value) && strlen(value) > 0
)

procedure(AiviVCLibMgrEnvEnabled(name defaultValue)
  prog((value lower)
    value = getShellEnvVar(name)
    unless(AiviVCLibMgrNonEmpty(value)
      return(defaultValue)
    )
    lower = lowerCase(value)
    return(!(lower == "0" || lower == "false" || lower == "no" || lower == "off"))
  )
)

procedure(AiviVCLibMgrSkillDir()
  prog((dir)
    dir = getShellEnvVar("AIVIVC_SKILL_DIR")
    when(AiviVCLibMgrNonEmpty(dir)
      return(dir)
    )
    dir = getShellEnvVar("AIVIVC_ROOT")
    when(AiviVCLibMgrNonEmpty(dir)
      return(strcat(dir "/src/aivivc/skill"))
    )
    return(nil)
  )
)

procedure(AiviVCLoadCallbacks()
  prog((file)
    file = strcat(AiviVCLibMgrSkillDir() "/aivivcCallbacks.il")
    unless(isFile(file)
      printf("AIVIVC: callback file not found: %s\n" file)
      return(nil)
    )
    load(file)
    return(t)
  )
)

procedure(AiviVCLibMgrLoadExpr()
  sprintf(nil "load(\"%s/cdsLibMgr.il\")" AiviVCLibMgrSkillDir())
)

procedure(AiviVCLibMgrErrValue(expr)
  prog((result)
    result = errset(eval(expr))
    when(result
      return(car(result))
    )
    return(nil)
  )
)

procedure(AiviVCLibMgrLibTagHasAivivc(libPath)
  prog((tag port line lower found)
    unless(AiviVCLibMgrNonEmpty(libPath)
      return(nil)
    )
    tag = strcat(libPath "/cdsinfo.tag")
    unless(isFile(tag)
      return(nil)
    )
    port = infile(tag)
    unless(port
      return(nil)
    )
    found = nil
    while(!found && (line = gets(line port))
      lower = lowerCase(line)
      when(rexMatchp("^[ \t]*dmtype[ \t]+aivivc" lower)
        found = t
      )
    )
    close(port)
    return(found)
  )
)

procedure(AiviVCLibMgrForceLibDmTypes()
  prog((debug libs libId libName libPath dmSys result)
    unless(AiviVCLibMgrEnvEnabled("AIVIVC_FORCE_LIB_DMTYPE" t)
      return(nil)
    )
    unless(getd('ddGetLibList) && getd('ddGetObjWritePath) && getd('ddSetLibDmType)
      return(nil)
    )

    debug = AiviVCLibMgrEnvEnabled("AIVIVC_DEBUG" nil)
    libs = ddGetLibList()
    foreach(libId libs
      libName = libId~>name
      libPath = AiviVCLibMgrErrValue(list('ddGetObjWritePath libId))
      when(libPath && AiviVCLibMgrLibTagHasAivivc(libPath)
        dmSys = nil
        when(getd('ddGetPathDMSys)
          dmSys = AiviVCLibMgrErrValue(list('ddGetPathDMSys libPath))
        )
        unless(equal(dmSys "aivivc")
          result = AiviVCLibMgrErrValue(list('ddSetLibDmType libId "aivivc"))
          when(debug
            printf("AIVIVC: lmgr force ddSetLibDmType lib=%L path=%L before=%L result=%L\n"
                   libName libPath dmSys result)
          )
        )
      )
    )
    return(t)
  )
)

procedure(AiviVCLibMgrDebugInit()
  prog((cwd root cdsWorkarea cdsSite path dmSys icon resolved libs libId libName libPath usingDM objDmSys)
    unless(AiviVCLibMgrEnvEnabled("AIVIVC_DEBUG" nil)
      return(nil)
    )

    when(isCallable('getWorkingDir)
      cwd = getWorkingDir()
    )
    root = getShellEnvVar("AIVIVC_ROOT")
    cdsWorkarea = getShellEnvVar("CDS_WORKAREA")
    cdsSite = getShellEnvVar("CDS_SITE")

    printf("AIVIVC: lmgr init cwd=%L AIVIVC_ROOT=%L CDS_WORKAREA=%L CDS_SITE=%L\n"
           cwd root cdsWorkarea cdsSite)

    when(getd('ddGetPathDMSys)
      foreach(path list(cwd root cdsWorkarea)
        when(AiviVCLibMgrNonEmpty(path)
          dmSys = AiviVCLibMgrErrValue(list('ddGetPathDMSys path))
          printf("AIVIVC: lmgr ddGetPathDMSys(%L) => %L\n" path dmSys)
        )
      )
    )

    when(getd('lmgrValidateIcon)
      foreach(icon '("checked-in.png" "checked-out.png" "out-of-date.png" "locked.png")
        resolved = AiviVCLibMgrErrValue(list('lmgrValidateIcon icon))
        printf("AIVIVC: lmgr icon %s => %L\n" icon resolved)
      )
    )

    when(getd('ddGetLibList)
      libs = ddGetLibList()
      foreach(libId libs
        libName = libId~>name
        libPath = nil
        usingDM = nil
        objDmSys = nil
        dmSys = nil
        when(getd('ddGetObjWritePath)
          libPath = AiviVCLibMgrErrValue(list('ddGetObjWritePath libId))
        )
        when(getd('ddAmUsingDM)
          usingDM = AiviVCLibMgrErrValue(list('ddAmUsingDM libId))
        )
        when(getd('ddGetObjDMSys)
          objDmSys = AiviVCLibMgrErrValue(list('ddGetObjDMSys libId))
        )
        when(libPath && getd('ddGetPathDMSys)
          dmSys = AiviVCLibMgrErrValue(list('ddGetPathDMSys libPath))
        )
        printf("AIVIVC: lmgr lib name=%L path=%L ddAmUsingDM=%L ddGetObjDMSys=%L ddGetPathDMSys=%L\n"
               libName libPath usingDM objDmSys dmSys)
      )
    )
    return(t)
  )
)

procedure(AiviVCLibMgrInitCB()
  prog((menuLabel)
    when(AiviVCLibMgrMenuCreated
      return(t)
    )
    unless(AiviVCLoadCallbacks()
      return(nil)
    )
    unless(getd('lmgrCreateMenu)
      printf("AIVIVC: lmgrCreateMenu unavailable; load aivivcLibMgr.il from Library Manager context.\n")
      return(nil)
    )
    AiviVCLibMgrMenuCreated = t
    AiviVCLibMgrForceLibDmTypes()
    AiviVCLibMgrDebugInit()

    if(AiviVCLibMgrEnvEnabled("AIVIVC_REPLACE_DM_MENU" t) then
      errset(lmgrManageMenuItems(list("designCascade") nil))
      errset(lmgrManageMenuItems(list("aiviVCMenu") nil))
      menuLabel = "Design Manager"
    else
      menuLabel = "AIVIVC"
    )

    lmgrCreateMenu("aiviVCMenu"
                   list(list("label" menuLabel)
                        list("mapCallback" list("AiviVCLibMgrMapCB"))))
    lmgrAddMenuItems("" "" '("aiviVCMenu"))

    lmgrCreateMenuItem("AiviVCStatusItem" "simple"
                       '(("label" "Status...")
                         ("callback" ("AiviVCStatusCB"))))
    lmgrAddMenuItems("aiviVCMenu" "LCVlct" '("AiviVCStatusItem"))

    lmgrCreateMenuItem("AiviVCVersionItem" "simple"
                       '(("label" "Version Info...")
                         ("callback" ("AiviVCVersionCB"))))
    lmgrAddMenuItems("aiviVCMenu" "LCVlct" '("AiviVCVersionItem"))

    lmgrCreateMenuItem("AiviVCHistoryItem" "simple"
                       '(("label" "History...")
                         ("callback" ("AiviVCHistoryCB"))))
    lmgrAddMenuItems("aiviVCMenu" "LCVlct" '("AiviVCHistoryItem"))

    lmgrCreateMenuItem("AiviVCSeparator1" "separator" '(("callback" (""))))
    lmgrAddMenuItems("aiviVCMenu" "t" '("AiviVCSeparator1"))

    lmgrCreateMenuItem("AiviVCUpdateItem" "simple"
                       '(("label" "Update")
                         ("callback" ("AiviVCUpdateCB"))))
    lmgrAddMenuItems("aiviVCMenu" "LCVlct" '("AiviVCUpdateItem"))

    lmgrCreateMenuItem("AiviVCMakeEditableItem" "simple"
                       '(("label" "Make Editable")
                         ("callback" ("AiviVCMakeEditableCB"))))
    lmgrAddMenuItems("aiviVCMenu" "LCVlct" '("AiviVCMakeEditableItem"))

    lmgrCreateMenuItem("AiviVCCommitItem" "simple"
                       '(("label" "Commit...")
                         ("callback" ("AiviVCCommitCB"))))
    lmgrAddMenuItems("aiviVCMenu" "LCVlct" '("AiviVCCommitItem"))

    lmgrCreateMenuItem("AiviVCCancelItem" "simple"
                       '(("label" "Cancel Edit...")
                         ("callback" ("AiviVCCancelCB"))))
    lmgrAddMenuItems("aiviVCMenu" "LCVlct" '("AiviVCCancelItem"))
    return(t)
  )
)

procedure(AiviVCRegisterLibMgr()
  prog(()
    when(AiviVCLibMgrRegistered
      return(t)
    )
    AiviVCLoadCallbacks()

    when(getd('lmgrCreateMenu)
      AiviVCLibMgrRegistered = t
      AiviVCLibMgrInitCB()
      return(t)
    )

    if(getd('lmgrDefineInits) then
      lmgrDefineInits(list(AiviVCLibMgrLoadExpr()) nil)
      AiviVCLibMgrRegistered = t
      printf("AIVIVC: registered Library Manager load callback.\n")
      return(t)
    )

    unless(AiviVCLibMgrRegisterWarned
      printf("AIVIVC: lmgrDefineInits unavailable; open LM or call AiviVCRegisterLibMgr() later.\n")
      AiviVCLibMgrRegisterWarned = t
    )

    AiviVCLibMgrRegisterRetries = AiviVCLibMgrRegisterRetries + 1
    when(getd('hiRegTimer) && AiviVCLibMgrRegisterRetries <= 20
      hiRegTimer("AiviVCRegisterLibMgr()" 30)
    )
    return(nil)
  )
)

AiviVCRegisterLibMgr()
