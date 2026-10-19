# The export report

After an export, WhiteoutDex tells you only what needs your attention. When everything is fine, you see nothing and the file is just there.

When something needs your attention, a window **WhiteoutDex Export** opens:

- The first line counts the errors and warnings: *Export completed with 0 error(s) and 2 warning(s).*
- Each following line names one object and the problem, for example `WARNING: [Cape] …`.
- At most 50 lines are shown. The full list is in the file `mdlx_export_debug.log` (see *Files and settings*).

## Errors and warnings

| Kind | Meaning | What you do |
|---|---|---|
| **ERROR** | Something could not be exported. The model may be broken. | Fix the named object and export again. |
| **WARNING** | The model works, but something may look different than in 3ds Max. | Read the line. Often the scene check (*Fix all*) repairs it. |

!!! tip
    Run [[t:exp_fix_all_btn]] in the export window first. It repairs the most common problems before the export, so the report stays short.

The importer has the same kind of report, titled **WhiteoutDex Import**.
