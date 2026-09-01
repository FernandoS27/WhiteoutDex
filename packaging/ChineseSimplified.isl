; *** Inno Setup — Simplified Chinese messages for the WhiteoutDex installer ***
;
; Inno Setup 6 does not ship a Simplified Chinese language file, so this one is
; bundled with the project. It is loaded LAYERED OVER Default.isl (see the
; [Languages] entry in Whiteout_Setup.iss.in: the comma means later files win),
; which is what lets it be a partial translation rather than all 296 messages.
;
; What is translated: every message this installer can actually put on screen.
; The wizard is deliberately minimal — ShowLanguageDialog=no, DisableDirPage,
; DisableProgramGroupPage, no [Components] and no [Tasks] — so the flow is
; welcome, licence, ready, installing, finished, plus the uninstaller and the
; errors that can interrupt any of those.
;
; What is not: pages and failure modes this installer never reaches (component
; and Start Menu selection, downloads, archive extraction, password prompts,
; shared-file and disk-change handling). Those fall through to the English in
; Default.isl, which is honest — a wrong-but-Chinese string would be worse than
; an English one nobody sees.
;
; To swap in the full community translation from
; https://jrsoftware.org/files/istrans/: replace this file and drop the
; "compiler:Default.isl," prefix from the [Languages] entry.
;
; Encoding: UTF-8 with BOM, which is what Inno Setup 6 expects for a Unicode
; language file.

[LangOptions]
LanguageName=<7B80><4F53><4E2D><6587>
LanguageID=$0804
LanguageCodePage=936

[Messages]

; *** Application titles
SetupAppTitle=安装程序
SetupWindowTitle=%1 - 安装程序
UninstallAppTitle=卸载
UninstallAppFullTitle=%1 卸载

; *** Misc. common
InformationTitle=信息
ConfirmTitle=确认
ErrorTitle=错误

; *** Misc. errors
LastErrorMessage=%1。%n%n错误 %2: %3
SetupFileMissing=安装目录中缺少文件 %1。%n%n请更正该问题，或获取一份新的程序副本。
SetupFileCorrupt=安装文件已损坏。请获取一份新的程序副本。
SetupFileCorruptOrWrongVer=安装文件已损坏，或与此版本的安装程序不兼容。请更正该问题，或获取一份新的程序副本。
InvalidParameter=命令行中传入了无效参数：%n%n%1
SetupAlreadyRunning=安装程序已在运行。
WindowsVersionNotSupported=此程序不支持当前运行的 Windows 版本。
AdminPrivilegesRequired=安装此程序时必须以管理员身份登录。
PowerUserPrivilegesRequired=安装此程序时必须以管理员或超级用户组成员身份登录。
SetupAppRunningError=安装程序检测到 %1 正在运行。%n%n请立即关闭它的所有窗口，然后点击“确定”继续，或点击“取消”退出。
UninstallAppRunningError=卸载程序检测到 %1 正在运行。%n%n请立即关闭它的所有窗口，然后点击“确定”继续，或点击“取消”退出。

; *** Startup questions
ExitSetupTitle=退出安装程序
ExitSetupMessage=安装尚未完成。如果现在退出，程序将不会被安装。%n%n你可以稍后再次运行安装程序以完成安装。%n%n要退出安装程序吗？
AboutSetupMenuItem=关于安装程序(&A)...
AboutSetupTitle=关于安装程序
AboutSetupMessage=%1 版本 %2%n%3%n%n%1 主页：%n%4
AboutSetupNote=
TranslatorNote=

; *** Buttons
ButtonBack=< 上一步(&B)
ButtonNext=下一步(&N) >
ButtonInstall=安装(&I)
ButtonOK=确定
ButtonCancel=取消
ButtonYes=是(&Y)
ButtonYesToAll=全部是(&A)
ButtonNo=否(&N)
ButtonNoToAll=全部否(&O)
ButtonFinish=完成(&F)
ButtonBrowse=浏览(&B)...
ButtonWizardBrowse=浏览(&R)...
ButtonNewFolder=新建文件夹(&M)

; *** "Select Language" dialog messages
SelectLanguageTitle=选择安装语言
SelectLanguageLabel=选择安装过程中使用的语言。

; *** Common wizard text
ClickNext=点击“下一步”继续，或点击“取消”退出安装程序。
BeveledLabel=
BrowseDialogTitle=浏览文件夹
BrowseDialogLabel=在下面的列表中选择一个文件夹，然后点击“确定”。
NewFolderName=新建文件夹

; *** "Welcome" wizard page
WelcomeLabel1=欢迎使用 [name] 安装向导
WelcomeLabel2=即将在你的计算机上安装 [name/ver]。%n%n建议在继续之前关闭其他所有应用程序。

; *** "License Agreement" wizard page
WizardLicense=许可协议
LicenseLabel=请在继续之前阅读以下重要信息。
LicenseLabel3=请阅读以下许可协议。你必须接受本协议的条款才能继续安装。
LicenseAccepted=我接受协议(&A)
LicenseNotAccepted=我不接受协议(&D)

; *** "Information" wizard pages
WizardInfoBefore=信息
InfoBeforeLabel=请在继续之前阅读以下重要信息。
InfoBeforeClickLabel=准备好继续安装后，请点击“下一步”。
WizardInfoAfter=信息
InfoAfterLabel=请在继续之前阅读以下重要信息。
InfoAfterClickLabel=准备好继续安装后，请点击“下一步”。

; *** "Select Destination Location" wizard page
WizardSelectDir=选择安装位置
SelectDirDesc=[name] 应安装到哪里？
SelectDirLabel3=安装程序会将 [name] 安装到下面的文件夹中。
SelectDirBrowseLabel=点击“下一步”继续。若要选择其他文件夹，请点击“浏览”。
DiskSpaceGBLabel=至少需要 [gb] GB 可用磁盘空间。
DiskSpaceMBLabel=至少需要 [mb] MB 可用磁盘空间。
InvalidPath=必须输入包含盘符的完整路径，例如：%n%nC:\APP%n%n或以下形式的 UNC 路径：%n%n\\server\share
InvalidDrive=所选的驱动器或 UNC 共享不存在或无法访问。请另选一个。
DiskSpaceWarningTitle=磁盘空间不足
DiskSpaceWarning=安装至少需要 %1 KB 的可用空间，但所选驱动器只有 %2 KB 可用。%n%n仍要继续吗？
DirNameTooLong=文件夹名称或路径过长。
InvalidDirName=文件夹名称无效。
BadDirName32=文件夹名称不能包含以下任何字符：%n%n%1
DirExistsTitle=文件夹已存在
DirExists=文件夹：%n%n%1%n%n已存在。仍要安装到该文件夹吗？
DirDoesntExistTitle=文件夹不存在
DirDoesntExist=文件夹：%n%n%1%n%n不存在。是否创建该文件夹？

; *** "Ready to Install" wizard page
WizardReady=准备安装
ReadyLabel1=安装程序现在准备开始在你的计算机上安装 [name]。
ReadyLabel2a=点击“安装”开始安装。若要检查或更改任何设置，请点击“上一步”。
ReadyLabel2b=点击“安装”开始安装。
ReadyMemoUserInfo=用户信息：
ReadyMemoDir=安装位置：
ReadyMemoType=安装类型：
ReadyMemoComponents=所选组件：
ReadyMemoGroup=开始菜单文件夹：
ReadyMemoTasks=附加任务：

; *** "Preparing to Install" wizard page
WizardPreparing=正在准备安装
PreparingDesc=安装程序正在准备在你的计算机上安装 [name]。
PreviousInstallNotCompleted=先前程序的安装或卸载尚未完成。你需要重新启动计算机以完成该操作。%n%n重启后请再次运行安装程序以完成 [name] 的安装。
CannotContinue=安装程序无法继续。请点击“取消”退出。
ApplicationsFound=以下应用程序正在使用需要由安装程序更新的文件。建议允许安装程序自动关闭这些应用程序。
ApplicationsFound2=以下应用程序正在使用需要由安装程序更新的文件。建议允许安装程序自动关闭这些应用程序。安装完成后，安装程序会尝试重新启动它们。
CloseApplications=自动关闭这些应用程序(&A)
DontCloseApplications=不要关闭这些应用程序(&D)
ErrorCloseApplications=安装程序无法自动关闭所有应用程序。建议在继续之前手动关闭所有正在使用需更新文件的应用程序。
PrepareToInstallNeedsRestart=安装程序需要重新启动计算机。重启后请再次运行安装程序以完成 [name] 的安装。%n%n是否立即重新启动？

; *** "Installing" wizard page
WizardInstalling=正在安装
InstallingLabel=请稍候，安装程序正在你的计算机上安装 [name]。

; *** "Setup Completed" wizard page
FinishedHeadingLabel=[name] 安装完成
FinishedLabelNoIcons=安装程序已在你的计算机上安装完 [name]。
FinishedLabel=安装程序已在你的计算机上安装完 [name]。
ClickFinish=点击“完成”退出安装程序。
FinishedRestartLabel=为完成 [name] 的安装，需要重新启动计算机。是否立即重启？
FinishedRestartMessage=为完成 [name] 的安装，需要重新启动计算机。%n%n是否立即重启？
ShowReadmeCheck=是，我想查看自述文件
YesRadio=是，立即重新启动计算机(&Y)
NoRadio=否，我稍后再重新启动计算机(&N)
RunEntryExec=运行 %1
RunEntryShellExec=查看 %1

; *** Setup common messages
SetupAborted=安装未完成。%n%n请更正该问题后再次运行安装程序。
AbortRetryIgnoreSelectAction=选择操作
AbortRetryIgnoreRetry=重试(&T)
AbortRetryIgnoreIgnore=忽略错误并继续(&I)
AbortRetryIgnoreCancel=取消安装
RetryCancelSelectAction=选择操作
RetryCancelRetry=重试(&T)
RetryCancelCancel=取消

; *** Status messages
StatusClosingApplications=正在关闭应用程序...
StatusCreateDirs=正在创建目录...
StatusExtractFiles=正在解压文件...
StatusCreateIcons=正在创建快捷方式...
StatusCreateIniEntries=正在创建 INI 条目...
StatusCreateRegistryEntries=正在创建注册表项...
StatusRegisterFiles=正在注册文件...
StatusSavingUninstall=正在保存卸载信息...
StatusRunProgram=正在完成安装...
StatusRestartingApplications=正在重新启动应用程序...
StatusRollback=正在撤销所做的更改...

; *** Misc. errors
ErrorInternal2=内部错误：%1
ErrorFunctionFailedNoCode=%1 失败
ErrorFunctionFailed=%1 失败；代码 %2
ErrorFunctionFailedWithMessage=%1 失败；代码 %2。%n%3
ErrorExecutingProgram=无法执行文件：%n%1
ErrorCreatingDir=安装程序无法创建目录“%1”
ErrorTooManyFilesInDir=无法在目录“%1”中创建文件，因为其中的文件过多

; *** Registry errors
ErrorRegOpenKey=打开注册表项时出错：%n%1\%2
ErrorRegCreateKey=创建注册表项时出错：%n%1\%2
ErrorRegWriteKey=写入注册表项时出错：%n%1\%2

; *** INI errors
ErrorIniEntry=在文件“%1”中创建 INI 条目时出错。

; *** File copying errors
FileAbortRetryIgnoreSkipNotRecommended=跳过此文件(&S)（不推荐）
FileAbortRetryIgnoreIgnoreNotRecommended=忽略错误并继续(&I)（不推荐）
SourceIsCorrupted=源文件已损坏
SourceDoesntExist=源文件“%1”不存在
ExistingFileReadOnly2=无法替换现有文件，因为它是只读的。
ExistingFileReadOnlyRetry=移除只读属性并重试(&R)
ExistingFileReadOnlyKeepExisting=保留现有文件(&K)
ErrorReadingExistingDest=读取现有文件时出错：
FileExistsSelectAction=选择操作
FileExists2=文件已存在。
FileExistsOverwriteExisting=覆盖现有文件(&O)
FileExistsKeepExisting=保留现有文件(&K)
FileExistsOverwriteOrKeepAll=对后续冲突执行相同操作(&D)
ExistingFileNewerSelectAction=选择操作
ExistingFileNewer2=现有文件比安装程序要写入的文件更新。
ExistingFileNewerOverwriteExisting=覆盖现有文件(&O)
ExistingFileNewerKeepExisting=保留现有文件(&K)（推荐）
ExistingFileNewerOverwriteOrKeepAll=对后续冲突执行相同操作(&D)
ErrorChangingAttr=更改现有文件属性时出错：
ErrorCreatingTemp=在目标目录中创建文件时出错：
ErrorReadingSource=读取源文件时出错：
ErrorCopying=复制文件时出错：
ErrorReplacingExistingFile=替换现有文件时出错：
ErrorRestartReplace=RestartReplace 失败：
ErrorRenamingTemp=在目标目录中重命名文件时出错：
ErrorRegisterServer=无法注册 DLL/OCX：%1
ErrorRegSvr32Failed=RegSvr32 失败，退出代码 %1
ErrorRegisterTypeLib=无法注册类型库：%1

; *** Uninstall display name markings
UninstallDisplayNameMark=%1 (%2)
UninstallDisplayNameMarks=%1 (%2, %3)
UninstallDisplayNameMark32Bit=32 位
UninstallDisplayNameMark64Bit=64 位
UninstallDisplayNameMarkAllUsers=所有用户
UninstallDisplayNameMarkCurrentUser=当前用户

; *** Post-installation errors
ErrorOpeningReadme=打开自述文件时出错。
ErrorRestartingComputer=安装程序无法重新启动计算机。请手动重启。

; *** Uninstaller messages
UninstallNotFound=文件“%1”不存在，无法卸载。
UninstallOpenError=无法打开文件“%1”，无法卸载。
UninstallUnsupportedVer=此版本的卸载程序无法识别卸载日志文件“%1”的格式，无法卸载
UninstallUnknownEntry=卸载日志中遇到未知条目 (%1)
ConfirmUninstall=确定要完全移除 %1 及其所有组件吗？
UninstallOnlyOnWin64=此安装只能在 64 位 Windows 上卸载。
OnlyAdminCanUninstall=此安装只能由具有管理员权限的用户卸载。
UninstallStatusLabel=正在从你的计算机中移除 %1，请稍候。
UninstalledAll=已成功从你的计算机中移除 %1。
UninstalledMost=%1 卸载完成。%n%n有些内容无法移除，可以手动删除。
UninstalledAndNeedsRestart=为完成 %1 的卸载，必须重新启动计算机。%n%n是否立即重启？
UninstallDataCorrupted=文件“%1”已损坏，无法卸载

; *** Uninstallation phase messages
ConfirmDeleteSharedFileTitle=删除共享文件？
ConfirmDeleteSharedFile2=系统显示以下共享文件已不再被任何程序使用。是否要卸载程序删除该共享文件？%n%n如果仍有程序在使用该文件而它被删除，这些程序可能无法正常运行。若不确定，请选择“否”，保留该文件不会对系统造成损害。
SharedFileNameLabel=文件名：
SharedFileLocationLabel=位置：
WizardUninstalling=卸载状态
StatusUninstalling=正在卸载 %1...

; *** Shutdown block reasons
ShutdownBlockReasonInstallingApp=正在安装 %1。
ShutdownBlockReasonUninstallingApp=正在卸载 %1。

[CustomMessages]
NameAndVersion=%1 版本 %2
AdditionalIcons=附加快捷方式：
CreateDesktopIcon=创建桌面快捷方式(&D)
CreateQuickLaunchIcon=创建快速启动栏快捷方式(&Q)
ProgramOnTheWeb=%1 网站
UninstallProgram=卸载 %1
LaunchProgram=启动 %1
AssocFileExtension=将 %2 文件扩展名与 %1 关联(&A)
AssocingFileExtension=正在将 %2 文件扩展名与 %1 关联...
AutoStartProgramGroupDescription=启动：
AutoStartProgram=自动启动 %1
AddonHostProgramNotFound=在你选择的文件夹中找不到 %1。%n%n仍要继续吗？
