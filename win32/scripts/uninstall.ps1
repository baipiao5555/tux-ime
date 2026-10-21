Start-Process -Verb RunAs powershell.exe -Args "-executionpolicy bypass -command Set-Location \`"$PWD\`"; regsvr32 /u build/dll/tuxime-x86_64.dll"
