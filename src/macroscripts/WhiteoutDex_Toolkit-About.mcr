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
		-- The installed layout puts the scripts under {app}\scripts\... and the
		-- icons under {app}\whiteoutdex_icons, so neither relative guess above
		-- hits: go through the install root the same way the sidebar does.
		try (
			if ::WhiteoutDexInstallRoot != undefined and ::WhiteoutDexInstallRoot != "" then
				append searchPaths (::WhiteoutDexInstallRoot + "\\whiteoutdex_icons\\")
		) catch ()
		try (
			append searchPaths ((systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\whiteoutdex_icons\\")
			append searchPaths ((systemTools.getEnvVariable "ALLUSERSPROFILE") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\whiteoutdex_icons\\")
		) catch ()
		append searchPaths ((getDir #userScripts) + "\\whiteoutdex_icons\\")
		append searchPaths ((getDir #scripts) + "\\whiteoutdex_icons\\")
		append searchPaths ((getDir #maxroot) + "\\whiteoutdex_icons\\")

		for p in searchPaths do
			if doesFileExist p then return p

		return undefined
	)

	fn getWhiteoutDexVersion =
	(
		local ver = undefined
		try (ver = ::WdxGetInstalledVersion()) catch ()
		if ver == undefined then "?.?.?" else ver
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
		local logoB64 = ""

		if iconFolder != undefined then
		(
			local ytPath = iconFolder + "youtube.png"
			local hwPath = iconFolder + "Hiveworkshop.png"
			local dcPath = iconFolder + "Discord.png"
			local ghPath = iconFolder + "GitHub-logo.png"
			local lgPath = iconFolder + "whiteoutdex_logo.png"
			if doesFileExist ytPath then youtubeB64 = imgToBase64 ytPath
			if doesFileExist hwPath then hivewsB64 = imgToBase64 hwPath
			if doesFileExist dcPath then discordB64 = imgToBase64 dcPath
			if doesFileExist ghPath then githubB64 = imgToBase64 ghPath
			if doesFileExist lgPath then logoB64 = imgToBase64 lgPath
		)

		local youtubeSrc = if youtubeB64 != "" then ("data:image/png;base64," + youtubeB64) else ""
		local hivewsSrc = if hivewsB64 != "" then ("data:image/png;base64," + hivewsB64) else ""
		local discordSrc = if discordB64 != "" then ("data:image/png;base64," + discordB64) else ""
		local githubSrc = if githubB64 != "" then ("data:image/png;base64," + githubB64) else ""
		local logoSrc = if logoB64 != "" then ("data:image/png;base64," + logoB64) else ""

		local html = "<!DOCTYPE html>\n"
		html += "<html>\n<head>\n<meta http-equiv='X-UA-Compatible' content='IE=edge'>\n"
		html += "<style>\n"

		html += "* { margin: 0; padding: 0; box-sizing: border-box; }\n"
		html += "body {\n"
		html += "  background: #292c31;\n"
		html += "  font-family: 'Segoe UI', Tahoma, sans-serif;\n"
		html += "  color: #e0e0e0;\n"
		html += "  padding: 28px 36px;\n"
		html += "  overflow-y: auto;\n"
		html += "  -webkit-user-select: none; user-select: none;\n"
		html += "}\n"

		html += "::-webkit-scrollbar { width: 6px; }\n"
		html += "::-webkit-scrollbar-track { background: #292c31; }\n"
		html += "::-webkit-scrollbar-thumb { background: #4a5560; border-radius: 3px; }\n"

		html += ".header { text-align: center; margin-bottom: 24px; }\n"
		html += ".header .app-logo { width: 96px; height: 96px; margin-bottom: 10px; }\n"
		html += ".header h1 { font-size: 28px; font-weight: 600; color: #fff; margin-bottom: 6px; letter-spacing: 2px; }\n"
		-- The three brand colours in one stroke: light blue, teal, purple.
		html += ".header .accent-line { width: 120px; height: 3px; background: linear-gradient(90deg, #6ec6ff, #2fd4c0 50%, #b088ff); margin: 0 auto 14px auto; border-radius: 2px; }\n"
		html += ".header p { font-size: 14px; color: #9aa4ae; line-height: 1.6; }\n"

		html += ".card { background: #32363d; border: 1px solid #414855; border-radius: 8px; padding: 22px 28px; margin-bottom: 16px; text-align: center; }\n"
		html += ".card-icon { max-height: 50px; width: auto; margin-bottom: 14px; }\n"
		html += ".card-title { font-size: 13px; color: #6ec6ff; text-transform: uppercase; letter-spacing: 1.5px; margin-bottom: 12px; }\n"
		-- Left rules rotate through the palette so the long cards stay
		-- distinguishable while scrolling.
		html += ".accent-blue { border-left: 3px solid #6ec6ff; }\n"
		html += ".accent-teal { border-left: 3px solid #2fd4c0; }\n"
		html += ".accent-purple { border-left: 3px solid #b088ff; }\n"

		html += ".license-card { text-align: left; }\n"
		html += ".license-card .card-title { text-align: center; }\n"
		html += ".license-text { font-family: 'Consolas', 'Courier New', monospace; font-size: 12px; color: #b6bec7; line-height: 1.7; white-space: pre-wrap; word-wrap: break-word; }\n"

		html += ".credits-card { text-align: left; }\n"
		html += ".credits-card .card-title { text-align: center; }\n"
		html += ".credits-section { margin-bottom: 14px; }\n"
		html += ".credits-section:last-child { margin-bottom: 0; }\n"
		html += ".credits-label { font-size: 11px; color: #2fd4c0; text-transform: uppercase; letter-spacing: 1.5px; margin-bottom: 6px; font-weight: 600; }\n"
		html += ".credits-names { font-size: 14px; color: #ccc; line-height: 1.7; }\n"
		html += ".credits-names .lead { color: #fff; font-weight: 600; }\n"
		html += ".credits-divider { border: none; border-top: 1px solid #414855; margin: 14px 0; }\n"

		html += ".btn { display: block; width: 100%; padding: 14px 28px; border: none; border-radius: 6px; font-size: 15px; font-weight: 600; cursor: pointer; text-decoration: none; color: #fff; text-align: center; letter-spacing: 0.5px; margin-bottom: 8px; }\n"
		html += ".btn:last-child { margin-bottom: 0; }\n"
		-- Buttons carry the palette rather than each site's own brand colour:
		-- light blue, teal and purple, one per destination.
		html += ".btn-youtube { background: linear-gradient(135deg, #6ec6ff, #2a8fd0); }\n"
		html += ".btn-youtube:hover { background: linear-gradient(135deg, #8fd6ff, #3ea3e6); }\n"
		html += ".btn-hive { background: linear-gradient(135deg, #2fd4c0, #128f80); }\n"
		html += ".btn-hive:hover { background: linear-gradient(135deg, #4ee8d4, #1aa895); }\n"
		html += ".btn-discord { background: linear-gradient(135deg, #8f7bff, #5b46c9); }\n"
		html += ".btn-discord:hover { background: linear-gradient(135deg, #a795ff, #6d58dd); }\n"
		html += ".btn-github { background: linear-gradient(135deg, #b088ff, #7040c8); }\n"
		html += ".btn-github:hover { background: linear-gradient(135deg, #c4a3ff, #8455dd); }\n"

		html += ".donate-section-divider { border: none; border-top: 1px solid #414855; margin: 16px 0; }\n"
		html += ".donate-subtitle { font-size: 12px; color: #6b7480; margin-bottom: 10px; }\n"

		html += ".footer { text-align: center; font-size: 13px; color: #6b7480; margin-top: 18px; }\n"
		html += ".footer span { color: #b088ff; }\n"

		html += "</style>\n"
		html += "</head>\n<body>\n"

		-- HEADER
		local ndxVer = getWhiteoutDexVersion()
		html += "<div class='header'>\n"
		if logoSrc != "" then
			html += "  <img class='app-logo' src='" + logoSrc + "' />\n"
		html += "  <h1>WHITEOUTDEX</h1>\n"
		html += "  <div class='accent-line'></div>\n"
		html += "  <p style='font-size:16px; color:#6ec6ff; margin-bottom:10px;'>" + (::WdxL.t "about_version_lbl") + " " + ndxVer + "</p>\n"
		html += "  <p>" + (::WdxL.t "about_blurb_html") + "</p>\n"
		html += "</div>\n"

		-- DOWNLOADS (Hive + GitHub combined)
		html += "<div class='card'>\n"
		html += "  <div class='card-title'>" + (::WdxL.t "about_downloads_card") + "</div>\n"
		if hivewsSrc != "" then
			html += "  <img class='card-icon' src='" + hivewsSrc + "' /><br>\n"
		html += "  <a class='btn btn-hive' href='action:hive'>" + (::WdxL.t "about_hive_btn") + "</a>\n"
		html += "  <hr class='donate-section-divider'>\n"
		if githubSrc != "" then
			html += "  <img class='card-icon' src='" + githubSrc + "' /><br>\n"
		html += "  <a class='btn btn-github' href='action:github'>" + (::WdxL.t "about_github_btn") + "</a>\n"
		html += "</div>\n"

		-- VIDEO CHANNEL
		html += "<div class='card'>\n"
		html += "  <div class='card-title'>" + (::WdxL.t "about_learn_card") + "</div>\n"
		if youtubeSrc != "" then
			html += "  <img class='card-icon' src='" + youtubeSrc + "' /><br>\n"
		html += "  <a class='btn btn-youtube' href='action:youtube'>" + (::WdxL.t "about_youtube_btn") + "</a>\n"
		html += "</div>\n"

		-- DISCORD
		html += "<div class='card'>\n"
		if discordSrc != "" then
			html += "  <img class='card-icon' src='" + discordSrc + "' /><br>\n"
		html += "  <div class='card-title'>" + (::WdxL.t "about_community_card") + "</div>\n"
		html += "  <a class='btn btn-discord' href='action:discord'>" + (::WdxL.t "about_discord_btn") + "</a>\n"
		html += "</div>\n"

		-- LICENSE — kept verbatim in step with LICENSE.md at the repo root, and
		-- deliberately NOT translated: a licence is only binding in the text it
		-- was granted in, so every language shows the same English BSD grant.
		-- The card's heading goes through the catalog; its body does not.
		html += "<div class='card license-card accent-blue'>\n"
		html += "  <div class='card-title'>" + (::WdxL.t "about_license_card") + "</div>\n"
		html += "  <div class='license-text'>"
		html += "BSD 3-Clause License\n\n"
		html += "Copyright (c) 2026, WhiteoutDex Contributors\n\n"
		html += "Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:\n\n"
		html += "1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.\n\n"
		html += "2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.\n\n"
		html += "3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.\n\n"
		html += "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
		html += "</div>\n"
		html += "</div>\n"

		-- DISCLAIMER
		html += "<div class='card license-card accent-teal'>\n"
		html += "  <div class='card-title'>" + (::WdxL.t "about_disclaimer_card") + "</div>\n"
		html += "  <div class='license-text'>"
		html += ::WdxL.t "about_disclaimer_body"
		html += "</div>\n"
		html += "</div>\n"

		-- CREDITS
		html += "<div class='card credits-card accent-purple'>\n"
		html += "  <div class='card-title'>" + (::WdxL.t "about_credits_card") + "</div>\n"

		html += "  <div class='credits-section'>\n"
		html += "    <div class='credits-label'>" + (::WdxL.t "about_lead_devs_lbl") + "</div>\n"
		html += "    <div class='credits-names'>\n"
		html += "      <span class='lead'>Fernando Sahmkow</span> <span style='color:#6b7480;'>(BlinkBoy)</span><br>\n"
		html += "      <span class='lead'>DennisH</span>\n"
		html += "    </div>\n"
		html += "  </div>\n"

		html += "  <hr class='credits-divider'>\n"

		html += "  <div class='credits-section'>\n"
		html += "    <div class='credits-label'>" + (::WdxL.t "about_contributors_lbl") + "</div>\n"
		html += "    <div class='credits-names'>\n"
		html += "      BlinkBoy (Fernando Sahmkow) <span style='color:#6b7480;'>(" + (::WdxL.t "about_role_original_author") + ")</span><br>\n"
		html += "      Republicola <span style='color:#6b7480;'>(" + (::WdxL.t "about_role_original_dexporter") + ")</span><br>\n"
		html += "      Igni <span style='color:#6b7480;'>(" + (::WdxL.t "about_role_biped_support") + ")</span><br>\n"
		html += "      BenSen <span style='color:#6b7480;'>(" + (::WdxL.t "about_role_features_fixes") + ")</span><br>\n"
		html += "      LxX'Studio <span style='color:#6b7480;'>(" + (::WdxL.t "about_role_plugins_fixes") + ")</span><br>\n"
		html += "      HuoHuoXiaoMao <span style='color:#6b7480;'>(" + (::WdxL.t "about_role_plugins_fixes") + ")</span><br>\n"
		html += "      &#x6653;&#x6708;&#x771F; XYZmoon <span style='color:#6b7480;'>(" + (::WdxL.t "about_role_icons") + ")</span>\n"
		html += "    </div>\n"
		html += "  </div>\n"

		html += "  <hr class='credits-divider'>\n"

		html += "  <div class='credits-section'>\n"
		html += "    <div class='credits-label'>" + (::WdxL.t "about_testers_lbl") + "</div>\n"
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
		-- Short list only; the full notices ship as THIRD_PARTY.md next to
		-- PackageContents.xml in the install root.
		html += "<div class='card credits-card accent-teal'>\n"
		html += "  <div class='card-title'>" + (::WdxL.t "about_thirdparty_card") + "</div>\n"

		html += "  <div class='credits-section'>\n"
		html += "    <div class='credits-label'>" + (::WdxL.t "about_libraries_lbl") + "</div>\n"
		html += "    <div class='credits-names'>\n"
		html += "      <span class='lead'>Dear ImGui</span> <span style='color:#6b7480;'>&#x2014; Omar Cornut (MIT)</span><br>\n"
		html += "      <span style='color:#6b7480;'>" + (::WdxL.t "about_thirdparty_note_html") + "</span>\n"
		html += "    </div>\n"
		html += "  </div>\n"

		html += "</div>\n"

		html += "<div class='footer'>" + (::WdxL.t "about_footer_html") + "</div>\n"
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
		panel.BackColor = dc.FromArgb 41 44 49

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

		-- Helper: create a link button. `accent` tints the caption with one of
		-- the three brand colours, mirroring the HTML buttons.
		fn makeLinkBtn text x y w h url accent =
		(
			local btn = dotNetObject "System.Windows.Forms.Button"
			btn.Text = text
			btn.Location = dotNetObject "System.Drawing.Point" x y
			btn.Size = dotNetObject "System.Drawing.Size" w h
			local flatStyle = (dotNetClass "System.Windows.Forms.FlatStyle").Flat
			btn.FlatStyle = flatStyle
			btn.FlatAppearance.BorderSize = 0
			btn.BackColor = (dotNetClass "System.Drawing.Color").FromArgb 50 54 61
			btn.ForeColor = accent
			btn.Font = dotNetObject "System.Drawing.Font" "Segoe UI" 10.0
			btn.Cursor = (dotNetClass "System.Windows.Forms.Cursors").Hand
			btn.Tag = url
			dotNet.addEventHandler btn "Click" (fn _click s e = (shellLaunch (s.Tag) ""))
			btn
		)

		local white  = dc.FromArgb 255 255 255
		local grey   = dc.FromArgb 154 164 174
		local blue   = dc.FromArgb 110 198 255
		local teal   = dc.FromArgb 47 212 192
		local purple = dc.FromArgb 176 136 255

		panel.Controls.Add (makeLabel "WHITEOUTDEX" 0 yPos 480 36 20.0 white)
		yPos += 40
		panel.Controls.Add (makeLabel ((::WdxL.t "about_version_lbl") + " " + ndxVer) 0 yPos 480 24 12.0 blue)
		yPos += 30
		panel.Controls.Add (makeLabel (::WdxL.t "about_blurb_short") 0 yPos 480 48 10.0 grey)
		yPos += 60

		panel.Controls.Add (makeLinkBtn (::WdxL.t "about_hive_btn") 40 yPos 400 32 "https://www.hiveworkshop.com/threads/sneak-peak-at-whiteoutdex-a-fully-native-toolset-for-3ds-max-based-on-neodex.371721/" teal)
		yPos += 40
		panel.Controls.Add (makeLinkBtn (::WdxL.t "about_github_btn") 40 yPos 400 32 "https://github.com/FernandoS27/WhiteoutDex" purple)
		yPos += 40
		panel.Controls.Add (makeLinkBtn (::WdxL.t "about_youtube_btn") 40 yPos 400 32 "https://www.youtube.com/@Wc3Tutorials" blue)
		yPos += 40
		panel.Controls.Add (makeLinkBtn (::WdxL.t "about_discord_btn") 40 yPos 400 32 "https://discord.gg/N8W9Rew2u" purple)
		yPos += 50

		panel.Controls.Add (makeLabel "BSD 3-Clause License — (c) 2026 WhiteoutDex Contributors" 0 yPos 480 20 8.0 (dc.FromArgb 107 116 128))
		yPos += 24
		panel.Controls.Add (makeLabel (::WdxL.t "about_footer_plain") 0 yPos 480 20 9.0 (dc.FromArgb 107 116 128))

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
		if ::WdxL != undefined then aboutTitle = ::WdxL.t "about_title"
		form.Text = aboutTitle
		form.Width = 500
		-- The content scrolls, but the frame is fixed - so never open taller
		-- than the desktop or the bottom of the dialog is unreachable.
		local wantHeight = 1100
		try
		(
			local work = (dotNetClass "System.Windows.Forms.Screen").PrimaryScreen.WorkingArea.Height
			if work > 200 and wantHeight > (work - 40) then wantHeight = work - 40
		)
		catch ()
		form.Height = wantHeight
		form.StartPosition = (dotNetClass "System.Windows.Forms.FormStartPosition").CenterScreen
		-- FixedSingle rather than FixedToolWindow: a tool window has no title
		-- bar icon, and this dialog is meant to carry the toolkit's.
		form.FormBorderStyle = (dotNetClass "System.Windows.Forms.FormBorderStyle").FixedSingle
		form.MinimizeBox = false
		form.MaximizeBox = false
		form.BackColor = (dotNetClass "System.Drawing.Color").FromArgb 41 44 49
		form.ShowInTaskbar = false
		form.TopMost = true
		try ( WdxSetFormIcon form ) catch ()

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
						shellLaunch "https://www.hiveworkshop.com/threads/sneak-peak-at-whiteoutdex-a-fully-native-toolset-for-3ds-max-based-on-neodex.371721/" ""
					)
					else if (findString url "action:discord") != undefined then
					(
						args.Cancel = true
						shellLaunch "https://discord.gg/N8W9Rew2u" ""
					)
					else if (findString url "action:github") != undefined then
					(
						args.Cancel = true
						shellLaunch "https://github.com/FernandoS27/WhiteoutDex" ""
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
