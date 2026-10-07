# Release|x64 の設定を丸ごと写して Demo|x64 を作る（2026-10-07）
local $/; my $t = <STDIN>;
my $C = q{'$(Configuration)|$(Platform)'=='Release|x64'};
my $D = q{'$(Configuration)|$(Platform)'=='Demo|x64'};
my $bs = chr(92);   # backslash
my $dll = "C:${bs}assimp${bs}5.2.5${bs}bin${bs}assimp-vc143-mtd.dll";
my $n = 0;
# 1) ProjectConfiguration
$t =~ s{(    <ProjectConfiguration Include="Release\|x64">\n.*?</ProjectConfiguration>\n)}{ my $b = $1; (my $d = $b) =~ s/Release/Demo/g; $n++; $b . $d }se;
# 2) Release|x64 の条件付きブロック（PropertyGroup / ImportGroup / ItemDefinitionGroup。Label が先の物もある）
$t =~ s{(  <(PropertyGroup|ImportGroup|ItemDefinitionGroup)(?: [^>]*?)? Condition="\Q$C\E"[^>]*>\n.*?</\2>\n)}{
    my $b = $1; my $kind = $2; (my $d = $b) =~ s/\Q$C\E/$D/;
    if ($kind eq 'ItemDefinitionGroup') {
        $d =~ s/<PreprocessorDefinitions>NDEBUG;/<PreprocessorDefinitions>NDEBUG;VFXL_DEMO;/;
        my $post = "    <PostBuildEvent>\n      <Command>xcopy /Y /D \"$dll\" \"\$(OutDir)\"</Command>\n    </PostBuildEvent>\n";
        $d =~ s{(    </PreBuildEvent>\n)}{$1$post};
    }
    $n++; $b . $d }gse;
# 3) 1 行の条件付きの項目（ShaderType / ShaderModel / ObjectFileOutput / ExcludedFromBuild / WarningLevel / SDLCheck）
$t =~ s{^(\s+<(\w+) Condition="\Q$C\E">[^\n]*</\2>\n)}{ my $b = $1; (my $d = $b) =~ s/\Q$C\E/$D/; $n++; $b . $d }gme;
print STDERR "replaced $n\n";
print $t;
