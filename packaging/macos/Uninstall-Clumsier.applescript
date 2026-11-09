display dialog "Quit Clumsier and wait 70 seconds first. Uninstall removes the app and helper, but keeps presets and recovery logs. Continue?" buttons {"Cancel", "Uninstall"} default button "Cancel" cancel button "Cancel"
do shell script "'/Library/Application Support/Clumsier/Helper/uninstall.sh'" with administrator privileges
display dialog "Clumsier was removed. Presets and recovery logs were retained." buttons {"OK"} default button "OK"
