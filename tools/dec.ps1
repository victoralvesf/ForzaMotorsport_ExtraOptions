param([string]$proj,[string]$name,[string]$prog,[string]$out,[Parameter(ValueFromRemainingArguments=$true)][string[]]$addrs)
$env:JAVA_HOME="C:\Program Files\Eclipse Adoptium\jdk-25.0.4.101-hotspot"; $env:PATH="$env:JAVA_HOME\bin;$env:PATH"
$S="E:\Azimuth For FM5\ghidra"
& C:\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat "$S\$proj" $name -process $prog -noanalysis -readOnly -scriptPath "E:\Azimuth For FM5\tools\ghidra_scripts" -postScript Decomp.java $out @addrs *> "$out.log"
