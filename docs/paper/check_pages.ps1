# Have Microsoft Word open the paper, report its page count, and export
# a PDF beside it - the only reliable way to know how a .docx lays out.
#
#   powershell -ExecutionPolicy Bypass -File docs\paper\check_pages.ps1

$docx = Join-Path $PSScriptRoot 'HomeMesh_IEEE_Paper.docx'
$pdf  = [IO.Path]::ChangeExtension($docx, '.pdf')

$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0
try {
  $doc = $word.Documents.Open($docx, $false, $true)
  $doc.Repaginate()
  $pages = $doc.ComputeStatistics(2)   # wdStatisticPages
  $words = $doc.ComputeStatistics(0)   # wdStatisticWords
  $doc.ExportAsFixedFormat($pdf, 17)   # wdExportFormatPDF
  $doc.Close($false)
  Write-Host "pages: $pages   words: $words"
  Write-Host "pdf:   $pdf"
} finally {
  $word.Quit()
  [void][Runtime.InteropServices.Marshal]::ReleaseComObject($word)
}
