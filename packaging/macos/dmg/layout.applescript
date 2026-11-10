on run argv
    set volumeName to item 1 of argv
    tell application "Finder"
        tell disk volumeName
            open
            set current view of container window to icon view
            set toolbar visible of container window to false
            set statusbar visible of container window to false
            set bounds of container window to {160, 120, 880, 560}
            set options to icon view options of container window
            set arrangement of options to not arranged
            set icon size of options to 96
            set text size of options to 13
            set background picture of options to file ".background:background.png"
            set position of item "Clumsier.app" to {185, 205}
            set position of item "Applications" to {535, 205}
            update without registering applications
            delay 2
            close
            open
            delay 2
        end tell
    end tell
end run
