# PowerShell Code Signing and Unblock Utility for NavStudio
param(
    [string]$FilePath = ".\nav_editor.exe",
    [switch]$UnblockOnly = $false,
    [string]$PublisherName = "NavStudio Developer"
)

# 1. Always unblock the file (removes Mark of the Web / Zone.Identifier)
if (Test-Path $FilePath) {
    Unblock-File -Path $FilePath
    Write-Host "[OK] Removed Mark of the Web from: $FilePath" -ForegroundColor Green
} else {
    Write-Host "[Warning] File not found: $FilePath" -ForegroundColor Yellow
}

if ($UnblockOnly) {
    exit 0
}

# 2. Check if a local NavStudio signing certificate already exists
$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Where-Object { $_.Subject -match $PublisherName } | Select-Object -First 1

if (-not $cert) {
    Write-Host "[*] Creating new self-signed Code Signing certificate for '$PublisherName'..." -ForegroundColor Cyan
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject "CN=$PublisherName" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -NotAfter (Get-Date).AddYears(5) `
        -KeySpec Signature `
        -HashAlgorithm SHA256

    # Add to Trusted Root Certification Authorities for Current User so Windows trusts it locally
    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store "Root", "CurrentUser"
    $store.Open("ReadWrite")
    $store.Add($cert)
    $store.Close()
    Write-Host "[OK] Installed certificate into Trusted Root store (CurrentUser)." -ForegroundColor Green
} else {
    Write-Host "[OK] Found existing certificate: $($cert.Thumbprint)" -ForegroundColor Green
}

# 3. Sign the executable
if (Test-Path $FilePath) {
    Write-Host "[*] Signing $FilePath with Authenticode signature..." -ForegroundColor Cyan
    $sig = Set-AuthenticodeSignature -FilePath $FilePath -Certificate $cert -HashAlgorithm SHA256
    Write-Host "[OK] Signature status: $($sig.StatusMessage)" -ForegroundColor Green
    Write-Host "The application is now signed and trusted on this machine." -ForegroundColor Green
}
