@echo off
break > code.txt
for /r %%f in (*) do (
    echo %%f | findstr /i "\.git\ \third_party\ \Fonts\ \icons\ \Wallpaper\ \Firmware\ \Inbuilt Devices\ " >nul
    if errorlevel 1 (
        echo %%f | findstr /i "\.png$ \.raw$ \.svg$ \.ttf$ \.exe$ \.bin$ \.tar\.gz$ \.obj$ \.lib$ \.dll$ \.sys$ \.ico$ \.dat$" >nul
        if errorlevel 1 (
            echo %%f | findstr /i "\.sh$" >nul
            if errorlevel 1 (
                echo %%f | findstr /i "\\LICENSE$ \\README\.md$ \\code\.txt$" >nul
                if errorlevel 1 (
                    echo ================================================== >> code.txt
                    echo FILE: %%f >> code.txt
                    echo ================================================== >> code.txt
                    type "%%f" >> code.txt
                    echo. >> code.txt
                    echo. >> code.txt
                )
            )
        )
    )
)
echo Done! All source and text files (excluding .git and code.txt) have been exported to code.txt.