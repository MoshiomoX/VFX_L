# include の整理（2026-10-07）
#   usage: perl tidy_includes.pl [--apply] file...
#   .cpp : pch.h（強制インクルード）に入れた標準ライブラリの #include を消す、同じ #include の 2 回目を消す、
#          先頭の #include の塊（#include・空行だけで続く所）を「最初の 1 行（自分のヘッダ）→ "..." → 空行 → <...>」に並べ直す
#   .h   : 同じ #include の 2 回目だけ消す
use strict; use warnings;
my $apply = 0;
if (@ARGV && $ARGV[0] eq '--apply') { $apply = 1; shift @ARGV; }

my %pch = map { $_ => 1 } qw(algorithm cmath iostream fstream filesystem cstdlib string random chrono vector
    cstdio cstring cfloat climits unordered_map unordered_set utility functional memory cstdint cstddef cwchar);

my ($nFiles, $nRemovedPch, $nRemovedDup, $nReordered, $nSkipReorder) = (0, 0, 0, 0, 0);
for my $file (@ARGV) {
    open my $fh, '<:raw', $file or die "$file: $!";
    local $/; my $raw = <$fh>; close $fh;
    my $bom = ($raw =~ s/^\xEF\xBB\xBF//) ? "\xEF\xBB\xBF" : '';
    my $eol = ($raw =~ /\r\n/) ? "\r\n" : "\n";
    my $endsWithEol = ($raw =~ /\n\z/) ? 1 : 0;
    my @lines = split /\r?\n/, $raw, -1;
    pop @lines if $endsWithEol && @lines && $lines[-1] eq '';
    my $isCpp = ($file =~ /\.cpp$/);

    my $key = sub { my $l = shift; return undef unless $l =~ /^\s*#\s*include\s*([<"][^>"]+[>"])/; return $1; };
    # 1) 消す（pch の標準ライブラリ / 2 回目）
    my %seen; my @out; my $changed = 0;
    for my $l (@lines) {
        my $k = $key->($l);
        if (defined $k) {
            if ($isCpp && $k =~ /^<([\w\/]+)>$/ && $pch{$1}) { $nRemovedPch++; $changed = 1; next; }
            if ($seen{$k}++) { $nRemovedDup++; $changed = 1; next; }
        }
        push @out, $l;
    }
    # 2) 並べ直す（.cpp の先頭の塊。#define / #if / コメント行が混ざっていたら触らない）
    if ($isCpp) {
        my ($i0) = grep { defined $key->($out[$_]) } 0..$#out;
        if (defined $i0) {
            my $i1 = $i0; my $pure = 1;
            for (my $i = $i0; $i <= $#out; $i++) {
                my $l = $out[$i];
                if (defined $key->($l)) { $i1 = $i; next; }
                next if $l =~ /^\s*$/;
                if ($l =~ /^\s*(\/\/|#)/) {
                    # 塊の後ろにまだ #include があるなら、間に挟まったコメント / プリプロセッサ → 並べ直さない
                    my $more = grep { defined $key->($out[$_]) } ($i + 1)..$#out;
                    $pure = 0 if $more && $l !~ /^\s*\/\/\s*TEMP-TEST/;
                }
                last;
            }
            if ($pure) {
                my @block = @out[$i0..$i1];
                my @inc = grep { defined $key->($_) } @block;
                my $first = shift @inc;
                my @quote = grep { $key->($_) =~ /^"/ } @inc;
                my @angle = grep { $key->($_) =~ /^</ } @inc;
                my @new = ($first, @quote);
                push @new, '', @angle if @angle;
                if (join("\n", @new) ne join("\n", @block)) { splice @out, $i0, $i1 - $i0 + 1, @new; $changed = 1; $nReordered++; }
            } else { $nSkipReorder++; print "skip reorder: $file\n" if !$apply; }
        }
    }
    next unless $changed;
    $nFiles++;
    if ($apply) {
        open my $oh, '>:raw', $file or die "$file: $!";
        print $oh $bom, join($eol, @out), ($endsWithEol ? $eol : '');
        close $oh;
    }
}
print "files changed $nFiles, removed pch-std $nRemovedPch, removed dup $nRemovedDup, reordered $nReordered, reorder skipped $nSkipReorder\n";
