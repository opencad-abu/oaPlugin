; AIVIVC Library Manager callbacks and Git/SVN command bridge.
; This file is safe to load in CIW or Library Manager context. Menu creation
; still lives in aivivcLibMgr.il; the LM menu map callback is defined here
; because Library Manager may evaluate it in a separate SKILL context.

procedure(AiviVCBlank(value)
  if(stringp(value) then
    strlen(value) == 0
  else
    t
  )
)

procedure(AiviVCNonEmpty(value)
  stringp(value) && strlen(value) > 0
)

procedure(AiviVCEnvEnabled(name defaultValue)
  prog((value lower)
    value = getShellEnvVar(name)
    unless(AiviVCNonEmpty(value)
      return(defaultValue)
    )
    lower = lowerCase(value)
    return(!(lower == "0" || lower == "false" || lower == "no" || lower == "off"))
  )
)

procedure(AiviVCErrValue(expr)
  prog((result)
    result = errset(eval(expr))
    when(result
      return(car(result))
    )
    return(nil)
  )
)

procedure(AiviVCLibMgrMapDebug(lib cell view file)
  prog((libId libPath dmSys usingDM)
    unless(AiviVCEnvEnabled("AIVIVC_DEBUG" nil)
      return(nil)
    )
    unless(AiviVCNonEmpty(lib)
      return(nil)
    )
    unless(getd('ddGetObj)
      printf("AIVIVC: lmgr map lib=%s cell=%s view=%s file=%s ddGetObj unavailable\n"
             lib cell view file)
      return(nil)
    )

    libId = ddGetObj(lib)
    unless(libId
      printf("AIVIVC: lmgr map lib=%s cell=%s view=%s file=%s ddGetObj=nil\n"
             lib cell view file)
      return(nil)
    )

    when(getd('ddGetObjWritePath)
      libPath = AiviVCErrValue(list('ddGetObjWritePath libId))
    )
    when(libPath && getd('ddGetPathDMSys)
      dmSys = AiviVCErrValue(list('ddGetPathDMSys libPath))
    )
    when(getd('ddAmUsingDM)
      usingDM = AiviVCErrValue(list('ddAmUsingDM libId))
    )
    printf("AIVIVC: lmgr map lib=%s cell=%s view=%s file=%s libPath=%L ddAmUsingDM=%L ddGetPathDMSys=%L\n"
           lib cell view file libPath usingDM dmSys)
    return(t)
  )
)

procedure(AiviVCLibMgrMapCB(@rest args)
  prog((lib cell view file enabled items)
    lib = cadr(args)
    cell = caddr(args)
    view = cadddr(args)
    file = car(cddddr(args))
    unless(stringp(lib) lib = "")
    unless(stringp(cell) cell = "")
    unless(stringp(view) view = "")
    unless(stringp(file) file = "")
    AiviVCLibMgrMapDebug(lib cell view file)
    enabled = AiviVCNonEmpty(lib)
    items = '("AiviVCStatusItem" "AiviVCUpdateItem" "AiviVCMakeEditableItem"
              "AiviVCCommitItem" "AiviVCCancelItem" "AiviVCHistoryItem"
              "AiviVCVersionItem")
    when(getd('lmgrSensitizeMenuItems)
      lmgrSensitizeMenuItems(items enabled)
    )
    return(enabled)
  )
)

procedure(AiviVCShQuote(value)
  prog((s i n ch out)
    s = value
    unless(stringp(s)
      s = ""
    )
    out = "'"
    i = 1
    n = strlen(s)
    while(i <= n
      ch = substring(s i 1)
      if(ch == "'" then
        out = strcat(out "'\"'\"'")
      else
        out = strcat(out ch)
      )
      i = i + 1
    )
    return(strcat(out "'"))
  )
)

procedure(AiviVCGitCommand()
  prog((cmd)
    cmd = getShellEnvVar("AIVIVC_GIT_COMMAND")
    unless(AiviVCNonEmpty(cmd)
      cmd = getShellEnvVar("AIVIVC_GIT")
    )
    unless(AiviVCNonEmpty(cmd)
      cmd = "git"
    )
    return(cmd)
  )
)

procedure(AiviVCSvnCommand()
  prog((cmd)
    cmd = getShellEnvVar("AIVIVC_SVN_COMMAND")
    unless(AiviVCNonEmpty(cmd)
      cmd = getShellEnvVar("AIVIVC_SVN")
    )
    unless(AiviVCNonEmpty(cmd)
      cmd = "svn"
    )
    return(cmd)
  )
)

procedure(AiviVCGdmAdapterCommand()
  prog((cmd)
    cmd = getShellEnvVar("AIVIVC_GDM_ADAPTER")
    unless(AiviVCNonEmpty(cmd)
      cmd = "aivivc-gdm"
    )
    return(cmd)
  )
)

procedure(AiviVCResolvePath(path)
  prog((root candidate cwd)
    unless(AiviVCNonEmpty(path)
      return(path)
    )
    when(substring(path 1 1) == "/"
      return(path)
    )

    root = getShellEnvVar("AIVIVC_ROOT")
    when(AiviVCNonEmpty(root)
      candidate = strcat(root "/" path)
      when(isDir(candidate) || isFile(candidate)
        return(candidate)
      )
    )

    when(isCallable('getWorkingDir)
      cwd = getWorkingDir()
      when(AiviVCNonEmpty(cwd)
        candidate = strcat(cwd "/" path)
        when(isDir(candidate) || isFile(candidate)
          return(candidate)
        )
      )
    )

    return(path)
  )
)

procedure(AiviVCTrimTrailingSlashes(path)
  prog((value n)
    value = path
    unless(AiviVCNonEmpty(value)
      return(value)
    )
    n = strlen(value)
    while(n > 1 && substring(value n 1) == "/"
      value = substring(value 1 (n - 1))
      n = n - 1
    )
    return(value)
  )
)

procedure(AiviVCParentDir(path)
  prog((value i)
    value = AiviVCTrimTrailingSlashes(path)
    unless(AiviVCNonEmpty(value)
      return(nil)
    )
    when(value == "/"
      return(nil)
    )
    i = strlen(value)
    while(i > 0 && substring(value i 1) != "/"
      i = i - 1
    )
    when(i <= 0
      return(nil)
    )
    when(i == 1
      return("/")
    )
    return(substring(value 1 (i - 1)))
  )
)

procedure(AiviVCFindAncestorMarker(path marker)
  prog((current candidate)
    current = AiviVCTrimTrailingSlashes(AiviVCResolvePath(path))
    while(AiviVCNonEmpty(current)
      candidate = strcat(current "/" marker)
      when(isDir(candidate) || isFile(candidate)
        return(current)
      )
      current = AiviVCParentDir(current)
    )
    return(nil)
  )
)

procedure(AiviVCDebug(message)
  when(AiviVCEnvEnabled("AIVIVC_DEBUG" nil)
    printf("AIVIVC: %s\n" message)
  )
)

procedure(AiviVCRunCommandInternal(cmd echoCommand)
  prog((pid output chunk)
    when(echoCommand
      printf("AIVIVC: %s\n" cmd)
    )
    pid = ipcBeginProcess(cmd)
    ipcWaitForProcess(pid)
    output = ""
    chunk = ipcReadProcess(pid 1)
    while(chunk
      output = strcat(output chunk)
      chunk = ipcReadProcess(pid 1)
    )
    return(output)
  )
)

procedure(AiviVCRunCommand(cmd)
  prog((output)
    output = AiviVCRunCommandInternal(cmd t)
    unless(AiviVCNonEmpty(output)
      output = "Command completed."
    )
    return(output)
  )
)

procedure(AiviVCRunQuiet(cmd)
  AiviVCRunCommandInternal(cmd nil)
)

procedure(AiviVCShow(title text)
  prog((message)
    message = sprintf(nil "%s\n\n%s" title text)
    printf("%s\n" message)
    if(isCallable('hiDisplayAppDBox) then
      hiDisplayAppDBox(
        ?name 'aiviVCMessageBox
        ?dboxBanner title
        ?dboxText text
        ?dialogType hicInformationDialog
        ?dialogStyle 'modal
        ?buttonLayout 'Close
      )
    else
      when(isCallable('lmgrDisplayMessage)
        lmgrDisplayMessage(strcat(message "\n"))
      )
    )
  )
)

procedure(AiviVCRefreshAfterAction(action)
  when(member(action '("makeEditable" "commit" "cancel" "update"))
    when(getd('ddUpdateLibList)
      errset(ddUpdateLibList())
    )
    when(getd('ddsRefresh)
      errset(ddsRefresh(?cellview t ?property t))
    )
  )
)

procedure(AiviVCConfirm(title text)
  prog((answer)
    if(isCallable('hiDisplayAppDBox) then
      answer = hiDisplayAppDBox(
        ?name 'aiviVCConfirmBox
        ?dboxBanner title
        ?dboxText text
        ?dialogType hicQuestionDialog
        ?dialogStyle 'modal
        ?buttonLayout 'YesNo
        ?defaultButton 2
      )
      return(answer)
    )
    return(t)
  )
)

procedure(AiviVCSelectionLabel(lib cell view file)
  prog((label)
    label = lib
    when(AiviVCNonEmpty(cell)
      label = sprintf(nil "%s/%s" lib cell)
    )
    when(AiviVCNonEmpty(view)
      label = sprintf(nil "%s/%s/%s" lib cell view)
    )
    when(AiviVCNonEmpty(file)
      label = sprintf(nil "%s/%s" label file)
    )
    return(label)
  )
)

procedure(AiviVCSelectedObject(args)
  prog((lib cell view file libId libPath objId path rel label)
    lib = cadr(args)
    cell = caddr(args)
    view = cadddr(args)
    file = car(cddddr(args))

    when(AiviVCBlank(lib)
      return(nil)
    )

    libId = ddGetObj(lib)
    unless(libId
      return(nil)
    )

    libPath = AiviVCResolvePath(ddGetObjWritePath(libId))
    unless(AiviVCNonEmpty(libPath)
      return(nil)
    )

    path = libPath
    when(AiviVCNonEmpty(cell)
      path = strcat(libPath "/" cell)
    )

    when(AiviVCNonEmpty(view)
      objId = ddGetObj(lib cell view "*")
      if(objId then
        path = AiviVCResolvePath(ddGetObjWritePath(objId))
        unless(AiviVCNonEmpty(path)
          path = strcat(libPath "/" cell "/" view)
        )
      else
        path = strcat(libPath "/" cell "/" view)
      )
    )

    when(AiviVCNonEmpty(file) && AiviVCNonEmpty(view)
      path = strcat(path "/" file)
    )

    if(path == libPath then
      rel = ""
    else
      rel = substring(path (strlen(libPath) + 2))
    )

    label = AiviVCSelectionLabel(lib cell view file)
    return(list(libPath path rel label))
  )
)

procedure(AiviVCDetectBackend(libPath)
  prog((cmd output git svn gitRoot svnRoot)
    git = AiviVCShQuote(AiviVCGitCommand())
    svn = AiviVCShQuote(AiviVCSvnCommand())
    svnRoot = AiviVCFindAncestorMarker(libPath ".svn")
    when(AiviVCNonEmpty(svnRoot)
      AiviVCDebug(sprintf(nil "svn marker found at %s for %s" svnRoot libPath))
      return("svn")
    )
    gitRoot = AiviVCFindAncestorMarker(libPath ".git")
    when(AiviVCNonEmpty(gitRoot)
      AiviVCDebug(sprintf(nil "git marker found at %s for %s" gitRoot libPath))
      return("git")
    )
    cmd = sprintf(nil "%s -C %s rev-parse --is-inside-work-tree 2>&1"
                  git AiviVCShQuote(libPath))
    output = AiviVCRunQuiet(cmd)
    AiviVCDebug(sprintf(nil "git probe for %s => %L" libPath output))
    when(output && rexMatchp("true" output)
      return("git")
    )
    cmd = sprintf(nil "%s info %s 2>&1" svn AiviVCShQuote(libPath))
    output = AiviVCRunQuiet(cmd)
    AiviVCDebug(sprintf(nil "svn probe for %s => %L" libPath output))
    when(output && (rexMatchp("Working Copy Root Path" output) ||
                    rexMatchp("Repository Root" output))
      return("svn")
    )
    return(nil)
  )
)

procedure(AiviVCCommitMessage()
  prog((msg)
    msg = getShellEnvVar("AIVIVC_COMMIT_MSG")
    unless(AiviVCNonEmpty(msg)
      msg = "AIVIVC commit from Virtuoso Library Manager"
    )
    return(msg)
  )
)

procedure(AiviVCBuildGitVersionCommand(libPath)
  prog((root git)
    git = AiviVCShQuote(AiviVCGitCommand())
    root = AiviVCShQuote(libPath)
    return(sprintf(nil "(%s -C %s branch --show-current; %s -C %s rev-parse --short HEAD; %s -C %s status -sb) 2>&1"
                   git root git root git root))
  )
)

procedure(AiviVCBuildSvnVersionCommand(targetPath)
  prog((path svn)
    svn = AiviVCShQuote(AiviVCSvnCommand())
    path = AiviVCShQuote(targetPath)
    return(sprintf(nil "%s info %s 2>&1" svn path))
  )
)

procedure(AiviVCBuildGdmCommand(action targetPath)
  prog((adapter path msg gdmAction)
    adapter = AiviVCShQuote(AiviVCGdmAdapterCommand())
    path = AiviVCShQuote(targetPath)
    msg = AiviVCShQuote(AiviVCCommitMessage())
    gdmAction = action
    case(action
      ("makeEditable" gdmAction = "co")
      ("commit" gdmAction = "ci")
      (t gdmAction = action)
    )

    case(gdmAction
      ("ci"
        return(sprintf(nil "%s ci -message %s -file %s 2>&1" adapter msg path))
      )
      (("status" "update" "co" "cancel" "history")
        return(sprintf(nil "%s %s -file %s 2>&1" adapter gdmAction path))
      )
    )
    return(nil)
  )
)

procedure(AiviVCActionTitle(action)
  case(action
    ("status" "AIVIVC Status")
    ("update" "AIVIVC Update")
    ("makeEditable" "AIVIVC Make Editable")
    ("commit" "AIVIVC Commit")
    ("cancel" "AIVIVC Cancel Edit")
    ("history" "AIVIVC History")
    ("version" "AIVIVC Version Info")
    (t "AIVIVC")
  )
)

procedure(AiviVCDoAction(action args)
  prog((selection libPath targetPath targetRel label backend cmd output)
    selection = AiviVCSelectedObject(args)
    unless(selection
      AiviVCShow("AIVIVC" "Select a library, cell, view, or file in Library Manager.")
      return(nil)
    )

    libPath = car(selection)
    targetPath = cadr(selection)
    targetRel = caddr(selection)
    label = cadddr(selection)
    AiviVCDebug(sprintf(nil "selection label=%s libPath=%s targetPath=%s targetRel=%s"
                        label libPath targetPath targetRel))
    backend = AiviVCDetectBackend(libPath)

    unless(backend
      AiviVCShow("AIVIVC" sprintf(nil "No Git or SVN work area found for %s." label))
      return(nil)
    )

    when(action == "cancel"
      unless(AiviVCConfirm("AIVIVC Cancel Edit"
             sprintf(nil "Discard local changes for %s?" label))
        return(nil)
      )
    )

    if(action == "version" then
      if(backend == "git" then
        cmd = AiviVCBuildGitVersionCommand(libPath)
      else
        cmd = AiviVCBuildSvnVersionCommand(targetPath)
      )
    else
      cmd = AiviVCBuildGdmCommand(action targetPath)
    )

    unless(cmd
      AiviVCShow("AIVIVC" sprintf(nil "Unsupported action: %s" action))
      return(nil)
    )

    output = AiviVCRunCommand(cmd)
    AiviVCRefreshAfterAction(action)
    AiviVCShow(AiviVCActionTitle(action) output)
    return(t)
  )
)

procedure(AiviVCStatusCB(@rest args)
  AiviVCDoAction("status" args)
)

procedure(AiviVCUpdateCB(@rest args)
  AiviVCDoAction("update" args)
)

procedure(AiviVCMakeEditableCB(@rest args)
  AiviVCDoAction("makeEditable" args)
)

procedure(AiviVCCommitCB(@rest args)
  AiviVCDoAction("commit" args)
)

procedure(AiviVCCancelCB(@rest args)
  AiviVCDoAction("cancel" args)
)

procedure(AiviVCHistoryCB(@rest args)
  AiviVCDoAction("history" args)
)

procedure(AiviVCVersionCB(@rest args)
  AiviVCDoAction("version" args)
)
