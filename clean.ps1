Write-Host "cleaning up datasurf builds..." -Fore Yellow

if(Test-Path "./bin")
{
    rm "./bin/" -Recurse -Force
}

foreach($file in (gci *.o))
{
    if(Test-Path $file)
    {
        Remove-Item $file
    }
}

foreach($file in (gci *.exe))
{
    if(Test-Path $file)
    {
        Remove-Item $file
    }
}

foreach($file in (gci *.pdb))
{
    if(Test-Path $file)
    {
        Remove-Item $file
    }
}

Write-Host "cleaned puddle!`n" -Fore Green
