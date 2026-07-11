# Generates the deterministic impulse response used by conv_long.txt.
# The IR is xorshift32 noise under an exponential decay envelope (-60 dB at the
# tail), mono float32 at 48 kHz. The fixed seed makes every regeneration
# bit-identical, so benchmark runs stay comparable across machines and dates.
# The .wav itself is generated output and must not be committed.
param(
	[string]$OutPath = "$PSScriptRoot\ir_2s_48k.wav",
	[int]$SampleRate = 48000,
	[double]$Seconds = 2.0
)

$count = [int]($SampleRate * $Seconds)
$state = [uint32]0x12345678
$decay = [Math]::Pow(10.0, -60.0 / 20.0 / ($count - 1))

$stream = [System.IO.File]::Create($OutPath)
$writer = New-Object System.IO.BinaryWriter($stream)
try
{
	$dataBytes = $count * 4
	$writer.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
	$writer.Write([uint32](36 + $dataBytes))
	$writer.Write([System.Text.Encoding]::ASCII.GetBytes("WAVE"))
	$writer.Write([System.Text.Encoding]::ASCII.GetBytes("fmt "))
	$writer.Write([uint32]16)
	$writer.Write([uint16]3)            # WAVE_FORMAT_IEEE_FLOAT
	$writer.Write([uint16]1)            # mono
	$writer.Write([uint32]$SampleRate)
	$writer.Write([uint32]($SampleRate * 4))
	$writer.Write([uint16]4)
	$writer.Write([uint16]32)
	$writer.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
	$writer.Write([uint32]$dataBytes)

	$envelope = 1.0
	for ($i = 0; $i -lt $count; $i++)
	{
		# xorshift32
		$state = $state -bxor ($state -shl 13)
		$state = $state -bxor ($state -shr 17)
		$state = $state -bxor ($state -shl 5)
		$noise = (($state / [double][uint32]::MaxValue) * 2.0) - 1.0
		$writer.Write([float]($noise * $envelope * 0.25))
		$envelope *= $decay
	}
}
finally
{
	$writer.Dispose()
}

Write-Host "Wrote $count samples to $OutPath"
