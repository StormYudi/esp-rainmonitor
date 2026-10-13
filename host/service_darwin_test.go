//go:build darwin

package main

import (
	"strconv"
	"strings"
	"testing"
)

func TestLaunchdDomainAndPlist(t *testing.T) {
	if launchdDomain(false) != "system" {
		t.Fatalf("system domain %q", launchdDomain(false))
	}
	if !strings.HasPrefix(launchdDomain(true), "gui/") {
		t.Fatalf("gui domain %q", launchdDomain(true))
	}
	if !strings.HasSuffix(launchdPlistPath(false), "/Library/LaunchDaemons/"+serviceName+".plist") {
		t.Fatalf("system plist %q", launchdPlistPath(false))
	}
	if !strings.Contains(launchdPlistPath(true), "/Library/LaunchAgents/"+serviceName+".plist") {
		t.Fatalf("user plist %q", launchdPlistPath(true))
	}
	if launchdTarget(false) != "system/"+serviceName {
		t.Fatalf("target %q", launchdTarget(false))
	}
	_ = strconv.Itoa(0)
}
