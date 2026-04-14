macroScript WhiteoutDex_About
buttonText:"About..."
category:"WhiteoutDex Toolkit"
internalCategory:"WhiteoutDex Toolkit"
(
	global whiteoutdex_aboutForm = undefined

	fn findIconFolder =
	(
		local srcFile = getSourceFileName()
		if srcFile != undefined and srcFile != "" then
		(
			local scriptDir = getFilenamePath srcFile
			local p1 = scriptDir + "whiteoutdex_icons\\"
			if doesFileExist p1 then return p1
			local parentDir = pathConfig.removePathLeaf (trimRight scriptDir "\\")
			local p2 = parentDir + "\\whiteoutdex_icons\\"
			if doesFileExist p2 then return p2
		)

		local searchPaths = #()
		append searchPaths ((getDir #userScripts) + "\\whiteoutdex_icons\\")
		append searchPaths ((getDir #scripts) + "\\whiteoutdex_icons\\")
		append searchPaths ((getDir #maxroot) + "\\whiteoutdex_icons\\")

		for p in searchPaths do
			if doesFileExist p then return p

		return undefined
	)

	fn getWhiteoutDexVersion =
	(
		local ver = "?.?.?"
		try (
			local p = undefined
			local subKey = (dotNetClass "Microsoft.Win32.Registry").CurrentUser.OpenSubKey "Software\\WhiteoutDex"
			if subKey != undefined then ( p = subKey.GetValue "InstallPath"; subKey.Close() )
			if p == undefined then
				p = (dotNetClass "System.Environment").GetFolderPath (dotNetClass "System.Environment+SpecialFolder").ApplicationData + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex"
			local vf = p + "\\version.txt"
			if doesFileExist vf then (
				local f = openFile vf mode:"r"
				if f != undefined then ( ver = trimRight (trimLeft (readLine f)); close f )
			)
		) catch ()
		ver
	)

	fn imgToBase64 filePath =
	(
		if not doesFileExist filePath then return ""
		try
		(
			local bytes = (dotNetClass "System.IO.File").ReadAllBytes filePath
			(dotNetClass "System.Convert").ToBase64String bytes
		)
		catch ( "" )
	)

	fn buildHTML =
	(
		local iconFolder = findIconFolder()
		local youtubeB64 = ""
		local hivewsB64 = ""
		local discordB64 = ""
		local githubB64 = ""

		if iconFolder != undefined then
		(
			local ytPath = iconFolder + "youtube.png"
			local hwPath = iconFolder + "Hiveworkshop.png"
			local dcPath = iconFolder + "Discord.png"
			local ghPath = iconFolder + "GitHub-logo.png"
			if doesFileExist ytPath then youtubeB64 = imgToBase64 ytPath
			if doesFileExist hwPath then hivewsB64 = imgToBase64 hwPath
			if doesFileExist dcPath then discordB64 = imgToBase64 dcPath
			if doesFileExist ghPath then githubB64 = imgToBase64 ghPath
		)

		local youtubeSrc = if youtubeB64 != "" then ("data:image/png;base64," + youtubeB64) else ""
		local hivewsSrc = if hivewsB64 != "" then ("data:image/png;base64," + hivewsB64) else ""
		local discordSrc = if discordB64 != "" then ("data:image/png;base64," + discordB64) else ""
		local githubSrc = if githubB64 != "" then ("data:image/png;base64," + githubB64) else ""

		local html = "<!DOCTYPE html>\n"
		html += "<html>\n<head>\n<meta http-equiv='X-UA-Compatible' content='IE=edge'>\n"
		html += "<style>\n"

		html += "* { margin: 0; padding: 0; box-sizing: border-box; }\n"
		html += "body {\n"
		html += "  background: #2b2b2b;\n"
		html += "  font-family: 'Segoe UI', Tahoma, sans-serif;\n"
		html += "  color: #e0e0e0;\n"
		html += "  padding: 28px 36px;\n"
		html += "  overflow-y: auto;\n"
		html += "  -webkit-user-select: none; user-select: none;\n"
		html += "}\n"

		html += "::-webkit-scrollbar { width: 6px; }\n"
		html += "::-webkit-scrollbar-track { background: #2b2b2b; }\n"
		html += "::-webkit-scrollbar-thumb { background: #555; border-radius: 3px; }\n"

		html += ".header { text-align: center; margin-bottom: 24px; }\n"
		html += ".header h1 { font-size: 28px; font-weight: 600; color: #fff; margin-bottom: 6px; letter-spacing: 2px; }\n"
		html += ".header .accent-line { width: 80px; height: 3px; background: linear-gradient(90deg, #0070ba, #00a2ff); margin: 0 auto 14px auto; border-radius: 2px; }\n"
		html += ".header p { font-size: 14px; color: #999; line-height: 1.6; }\n"

		html += ".card { background: #363636; border: 1px solid #444; border-radius: 8px; padding: 22px 28px; margin-bottom: 16px; text-align: center; }\n"
		html += ".card-icon { max-height: 50px; width: auto; margin-bottom: 14px; }\n"
		html += ".card-title { font-size: 13px; color: #888; text-transform: uppercase; letter-spacing: 1.5px; margin-bottom: 12px; }\n"

		html += ".license-card { text-align: left; }\n"
		html += ".license-card .card-title { text-align: center; }\n"
		html += ".license-text { font-family: 'Consolas', 'Courier New', monospace; font-size: 12px; color: #bbb; line-height: 1.7; white-space: pre-wrap; word-wrap: break-word; }\n"

		html += ".credits-card { text-align: left; }\n"
		html += ".credits-card .card-title { text-align: center; }\n"
		html += ".credits-section { margin-bottom: 14px; }\n"
		html += ".credits-section:last-child { margin-bottom: 0; }\n"
		html += ".credits-label { font-size: 11px; color: #0099dd; text-transform: uppercase; letter-spacing: 1.5px; margin-bottom: 6px; font-weight: 600; }\n"
		html += ".credits-names { font-size: 14px; color: #ccc; line-height: 1.7; }\n"
		html += ".credits-names .lead { color: #fff; font-weight: 600; }\n"
		html += ".credits-divider { border: none; border-top: 1px solid #444; margin: 14px 0; }\n"

		html += ".btn { display: block; width: 100%; padding: 14px 28px; border: none; border-radius: 6px; font-size: 15px; font-weight: 600; cursor: pointer; text-decoration: none; color: #fff; text-align: center; letter-spacing: 0.5px; margin-bottom: 8px; }\n"
		html += ".btn:last-child { margin-bottom: 0; }\n"
		html += ".btn-youtube { background: linear-gradient(135deg, #ff0000, #cc0000); }\n"
		html += ".btn-youtube:hover { background: linear-gradient(135deg, #ff3333, #dd0000); }\n"
		html += ".btn-hive { background: linear-gradient(135deg, #e68a00, #b36b00); }\n"
		html += ".btn-hive:hover { background: linear-gradient(135deg, #ffaa00, #cc8800); }\n"
		html += ".btn-discord { background: linear-gradient(135deg, #5865F2, #4752C4); }\n"
		html += ".btn-discord:hover { background: linear-gradient(135deg, #6d79ff, #5865F2); }\n"
		html += ".btn-github { background: linear-gradient(135deg, #333, #24292e); }\n"
		html += ".btn-github:hover { background: linear-gradient(135deg, #555, #333); }\n"

		html += ".donate-section-divider { border: none; border-top: 1px solid #444; margin: 16px 0; }\n"
		html += ".donate-subtitle { font-size: 12px; color: #666; margin-bottom: 10px; }\n"

		html += ".footer { text-align: center; font-size: 13px; color: #555; margin-top: 18px; }\n"
		html += ".footer span { color: #e74c3c; }\n"

		html += "</style>\n"
		html += "</head>\n<body>\n"

		-- HEADER
		local ndxVer = getWhiteoutDexVersion()
		html += "<div class='header'>\n"
		html += "  <h1>WHITEOUTDEX</h1>\n"
		html += "  <div class='accent-line'></div>\n"
		html += "  <p style='font-size:16px; color:#0099dd; margin-bottom:10px;'>Version " + ndxVer + "</p>\n"
		html += "  <p>A comprehensive Warcraft III modeling toolkit<br>"
		html += "for Autodesk 3ds Max.<br><br>"
		html += "Import and export MDX/MDL models, manage materials,<br>"
		html += "particle emitters, animations, attachments, lights,<br>"
		html += "collision shapes, cameras and more.</p>\n"
		html += "</div>\n"

		-- DOWNLOADS (Hive + GitHub combined)
		html += "<div class='card'>\n"
		html += "  <div class='card-title'>Get Latest Version &amp; Source Code</div>\n"
		if hivewsSrc != "" then
			html += "  <img class='card-icon' src='" + hivewsSrc + "' /><br>\n"
		html += "  <a class='btn btn-hive' href='action:hive'>Download from Hive Workshop</a>\n"
		html += "  <hr class='donate-section-divider'>\n"
		if githubSrc != "" then
			html += "  <img class='card-icon' src='" + githubSrc + "' /><br>\n"
		html += "  <a class='btn btn-github' href='action:github'>View on GitHub</a>\n"
		html += "</div>\n"

		-- VIDEO CHANNEL
		html += "<div class='card'>\n"
		html += "  <div class='card-title'>Learn More about WhiteoutDex</div>\n"
		if youtubeSrc != "" then
			html += "  <img class='card-icon' src='" + youtubeSrc + "' /><br>\n"
		html += "  <a class='btn btn-youtube' href='action:youtube'>Visit YouTube Channel</a>\n"
		html += "</div>\n"

		-- DISCORD
		html += "<div class='card'>\n"
		if discordSrc != "" then
			html += "  <img class='card-icon' src='" + discordSrc + "' /><br>\n"
		html += "  <div class='card-title'>Join the Community</div>\n"
		html += "  <a class='btn btn-discord' href='action:discord'>Join Discord Server</a>\n"
		html += "</div>\n"

		-- LICENSE
		html += "<div class='card license-card'>\n"
		html += "  <div class='card-title'>License</div>\n"
		html += "  <div class='license-text'>"
		html += "MIT License\n\n"
		html += "Copyright (c) 2026, Fernando Sahmkow &amp; DennisH\n\n"
		html += "Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:\n\n"
		html += "The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\n\n"
		html += "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE."
		html += "</div>\n"
		html += "</div>\n"

		-- DISCLAIMER
		html += "<div class='card license-card'>\n"
		html += "  <div class='card-title'>Disclaimer</div>\n"
		html += "  <div class='license-text'>"
		html += "WhiteoutDex Toolkit is a community-developed, fan-made modding tool for Warcraft III. It is not affiliated with, endorsed by, or in any way officially connected to Blizzard Entertainment, Inc. or any of its subsidiaries or affiliates.\n\n"
		html += "Warcraft, Warcraft III, Blizzard, and Blizzard Entertainment are trademarks or registered trademarks of Blizzard Entertainment, Inc.\n\n"
		html += "Autodesk and 3ds Max are trademarks or registered trademarks of Autodesk, Inc.\n\n"
		html += "All other trademarks are the property of their respective owners."
		html += "</div>\n"
		html += "</div>\n"

		-- CREDITS
		html += "<div class='card credits-card'>\n"
		html += "  <div class='card-title'>Credits</div>\n"

		html += "  <div class='credits-section'>\n"
		html += "    <div class='credits-label'>Lead Developers</div>\n"
		html += "    <div class='credits-names'>\n"
		html += "      <span class='lead'>Fernando Sahmkow</span> <span style='color:#666;'>(BlinkBoy)</span><br>\n"
		html += "      <span class='lead'>DennisH</span>\n"
		html += "    </div>\n"
		html += "  </div>\n"

		html += "  <hr class='credits-divider'>\n"

		html += "  <div class='credits-section'>\n"
		html += "    <div class='credits-label'>Contributors</div>\n"
		html += "    <div class='credits-names'>\n"
		html += "      BlinkBoy (Fernando Sahmkow) <span style='color:#666;'>(original author)</span><br>\n"
		html += "      Republicola <span style='color:#666;'>(original dexporter)</span><br>\n"
		html += "      Igni <span style='color:#666;'>(Bipped Support and fixes)</span><br>\n"
		html += "      BenSen <span style='color:#666;'>(new features and fixes)</span><br>\n"
		html += "      LxX'Studio <span style='color:#666;'>(Plugins and fixes)</span><br>\n"
		html += "      HuoHuoXiaoMao <span style='color:#666;'>(Plugins and fixes)</span><br>\n"
		html += "      &#x6653;&#x6708;&#x771F; XYZmoon <span style='color:#666;'>(Icons)</span>\n"
		html += "    </div>\n"
		html += "  </div>\n"

		html += "  <hr class='credits-divider'>\n"

		html += "  <div class='credits-section'>\n"
		html += "    <div class='credits-label'>Beta Testers</div>\n"
		html += "    <div class='credits-names'>\n"
		html += "      Adiktuz<br>\n"
		html += "      BallisticTerrain<br>\n"
		html += "      Manoo<br>\n"
		html += "      skrab<br>\n"
		html += "      GhostHeroine<br>\n"
		html += "      Black_XeSHTeG<br>\n"
		html += "      Gluma<br>\n"
		html += "      &#x2510;(&#xFFE3;&#x30D8;&#xFFE3;)&#x250C;\n"
		html += "    </div>\n"
		html += "  </div>\n"

		html += "</div>\n"

		-- THIRD-PARTY NOTICES
		html += "<div class='card credits-card'>\n"
		html += "  <div class='card-title'>Third-Party Software</div>\n"

		html += "  <div class='credits-section'>\n"
		html += "    <div class='credits-label'>WhiteoutTexCLI</div>\n"
		html += "    <div class='credits-names'>\n"
		html += "      BLP texture conversion powered by <span class='lead'>WhiteoutTexCLI.exe</span><br>\n"
		html += "      <span style='color:#666;'>Part of the WhiteoutTex project</span><br>\n"
		html += "      <a href='action:whiteoutlib' style='color:#0099dd; text-decoration:none;'>View WhiteoutLib License</a>\n"
		html += "    </div>\n"
		html += "  </div>\n"

		html += "  <hr class='credits-divider'>\n"

		html += "  <div class='credits-section'>\n"
		html += "    <div class='credits-label'>Included Libraries</div>\n"
		html += "    <div class='credits-names'>\n"
		html += "      <span class='lead'>Dear ImGui</span> <span style='color:#666;'>&#x2014; Omar Cornut (MIT)</span><br>\n"
		html += "      <span class='lead'>SDL</span> <span style='color:#666;'>&#x2014; Sam Lantinga (zlib)</span><br>\n"
		html += "      <span class='lead'>CascLib</span> <span style='color:#666;'>&#x2014; Ladislav Zezula (MIT)</span><br>\n"
		html += "      <span class='lead'>ncnn</span> <span style='color:#666;'>&#x2014; Tencent (BSD 3-Clause)</span><br>\n"
		html += "      <span class='lead'>Real-ESRGAN ncnn Vulkan</span> <span style='color:#666;'>&#x2014; Xintao Wang (MIT)</span>\n"
		html += "    </div>\n"
		html += "  </div>\n"

		html += "</div>\n"

		html += "<div class='footer'>Made with <span>&hearts;</span> by the WhiteoutDex Team</div>\n"
		html += "</body>\n</html>"

		return html
	)

	-- ============================================================================
	-- FALLBACK ABOUT DIALOG (if WebBrowser is unavailable, e.g. future .NET versions)
	-- ============================================================================
	fn showFallbackAbout form =
	(
		local ndxVer = getWhiteoutDexVersion()
		local dc = dotNetClass "System.Drawing.Color"
		local ca = dotNetClass "System.Drawing.ContentAlignment"

		local panel = dotNetObject "System.Windows.Forms.Panel"
		panel.Dock = (dotNetClass "System.Windows.Forms.DockStyle").Fill
		panel.AutoScroll = true
		panel.BackColor = dc.FromArgb 43 43 43

		local yPos = 20

		-- Helper: create a label
		fn makeLabel text x y w h fontSize fontColor =
		(
			local lbl = dotNetObject "System.Windows.Forms.Label"
			lbl.Text = text
			lbl.Location = dotNetObject "System.Drawing.Point" x y
			lbl.Size = dotNetObject "System.Drawing.Size" w h
			lbl.ForeColor = fontColor
			lbl.Font = dotNetObject "System.Drawing.Font" "Segoe UI" fontSize
			lbl.TextAlign = (dotNetClass "System.Drawing.ContentAlignment").TopCenter
			lbl
		)

		-- Helper: create a link button
		fn makeLinkBtn text x y w h url =
		(
			local btn = dotNetObject "System.Windows.Forms.Button"
			btn.Text = text
			btn.Location = dotNetObject "System.Drawing.Point" x y
			btn.Size = dotNetObject "System.Drawing.Size" w h
			local flatStyle = (dotNetClass "System.Windows.Forms.FlatStyle").Flat
			btn.FlatStyle = flatStyle
			btn.FlatAppearance.BorderSize = 0
			btn.BackColor = (dotNetClass "System.Drawing.Color").FromArgb 54 54 54
			btn.ForeColor = (dotNetClass "System.Drawing.Color").FromArgb 0 153 221
			btn.Font = dotNetObject "System.Drawing.Font" "Segoe UI" 10.0
			btn.Cursor = (dotNetClass "System.Windows.Forms.Cursors").Hand
			btn.Tag = url
			dotNet.addEventHandler btn "Click" (fn _click s e = (shellLaunch (s.Tag) ""))
			btn
		)

		local white = dc.FromArgb 255 255 255
		local grey  = dc.FromArgb 160 160 160
		local blue  = dc.FromArgb 0 153 221

		panel.Controls.Add (makeLabel "WHITEOUTDEX" 0 yPos 480 36 20.0 white)
		yPos += 40
		panel.Controls.Add (makeLabel ("Version " + ndxVer) 0 yPos 480 24 12.0 blue)
		yPos += 30
		panel.Controls.Add (makeLabel "A comprehensive Warcraft III modeling toolkit\nfor Autodesk 3ds Max." 0 yPos 480 48 10.0 grey)
		yPos += 60

		panel.Controls.Add (makeLinkBtn "Download from Hive Workshop" 40 yPos 400 32 "https://www.hiveworkshop.com/threads/whiteoutdex-3-2.354942/page-2")
		yPos += 40
		panel.Controls.Add (makeLinkBtn "View on GitHub" 40 yPos 400 32 "https://github.com/DennisHerrm/WhiteoutDex")
		yPos += 40
		panel.Controls.Add (makeLinkBtn "YouTube Channel" 40 yPos 400 32 "https://www.youtube.com/@Wc3Tutorials")
		yPos += 40
		panel.Controls.Add (makeLinkBtn "Join Discord" 40 yPos 400 32 "https://discord.gg/9xDRYYrPV3")
		yPos += 50

		panel.Controls.Add (makeLabel "MIT License — (c) 2026 DennisH & Fernando Sahmkow" 0 yPos 480 20 8.0 (dc.FromArgb 100 100 100))
		yPos += 24
		panel.Controls.Add (makeLabel "Made with love by the WhiteoutDex Team" 0 yPos 480 20 9.0 (dc.FromArgb 80 80 80))

		form.Controls.Add panel
	)

	-- ============================================================================
	-- EXECUTE
	-- ============================================================================

	on execute do
	(
		if whiteoutdex_aboutForm != undefined then
			try ( whiteoutdex_aboutForm.Close() ) catch ()

		local form = dotNetObject "System.Windows.Forms.Form"
		-- Localization
		local aboutTitle = "WhiteoutDex - About"
		if ::WdxL != undefined then aboutTitle = ::WdxL.t "about_about_macbtn"
		form.Text = aboutTitle
		form.Width = 500
		form.Height = 1100
		form.StartPosition = (dotNetClass "System.Windows.Forms.FormStartPosition").CenterScreen
		form.FormBorderStyle = (dotNetClass "System.Windows.Forms.FormBorderStyle").FixedToolWindow
		form.BackColor = (dotNetClass "System.Drawing.Color").FromArgb 43 43 43
		form.ShowInTaskbar = false
		form.TopMost = true

		local useWebBrowser = true

		-- Try WebBrowser (works on Max 2016-2025, may fail on Max 2026+ with .NET 8)
		try
		(
			local wb = dotNetObject "System.Windows.Forms.WebBrowser"
			wb.Dock = (dotNetClass "System.Windows.Forms.DockStyle").Fill
			wb.ScrollBarsEnabled = true
			wb.IsWebBrowserContextMenuEnabled = false
			wb.AllowNavigation = true
			wb.ScriptErrorsSuppressed = true

			wb.DocumentText = buildHTML()

			dotNet.addEventHandler wb "Navigating" \
			(
				fn onNavigating sender args =
				(
					local url = args.Url.ToString()

					if (findString url "action:youtube") != undefined then
					(
						args.Cancel = true
						shellLaunch "https://www.youtube.com/@Wc3Tutorials" ""
					)
					else if (findString url "action:hive") != undefined then
					(
						args.Cancel = true
						shellLaunch "https://www.hiveworkshop.com/threads/whiteoutdex-3-2.354942/page-2" ""
					)
					else if (findString url "action:discord") != undefined then
					(
						args.Cancel = true
						shellLaunch "https://discord.gg/9xDRYYrPV3" ""
					)
					else if (findString url "action:github") != undefined then
					(
						args.Cancel = true
						shellLaunch "https://github.com/DennisHerrm/WhiteoutDex" ""
					)
					else if (findString url "action:whiteoutlib") != undefined then
					(
						args.Cancel = true
						shellLaunch "https://github.com/FernandoS27/WhiteoutLib/blob/master/LICENSE-AI.md" ""
					)
					else if (findString url "about:blank") == undefined then
					(
						args.Cancel = true
					)
				)
			)

			form.Controls.Add wb
		)
		catch
		(
			-- WebBrowser failed (e.g. .NET 8 without IE support)
			format "WhiteoutDex About: WebBrowser unavailable, using fallback UI\n"
			useWebBrowser = false
			showFallbackAbout form
		)

		whiteoutdex_aboutForm = form
		form.Show()
	)
)
