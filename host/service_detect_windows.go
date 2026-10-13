//go:build windows

package main

import (
	"syscall"

	"golang.org/x/sys/windows"
	"golang.org/x/sys/windows/registry"
)

func detectService() serviceInfo {
	info := queryWindowsService(serviceName)
	if info.Installed {
		return info
	}
	if windowsServiceRegistered(serviceName) {
		return serviceInfo{Installed: true}
	}
	return serviceInfo{}
}

func queryWindowsService(name string) serviceInfo {
	mgr, err := windows.OpenSCManager(nil, nil, windows.SC_MANAGER_CONNECT)
	if err != nil {
		return serviceInfo{}
	}
	defer windows.CloseServiceHandle(mgr)

	svcName, err := syscall.UTF16PtrFromString(name)
	if err != nil {
		return serviceInfo{}
	}
	// Only ask for status. kardianos/service also requests START/STOP,
	// which a normal user cannot get for a system service.
	svc, err := windows.OpenService(mgr, svcName, windows.SERVICE_QUERY_STATUS)
	if err != nil {
		return serviceInfo{}
	}
	defer windows.CloseServiceHandle(svc)

	var st windows.SERVICE_STATUS
	if err := windows.QueryServiceStatus(svc, &st); err != nil {
		return serviceInfo{Installed: true}
	}
	info := serviceInfo{Installed: true}
	switch st.CurrentState {
	case windows.SERVICE_RUNNING, windows.SERVICE_START_PENDING:
		info.Running = true
	default:
		info.Stopped = true
	}
	return info
}

func windowsServiceRegistered(name string) bool {
	key, err := registry.OpenKey(registry.LOCAL_MACHINE, `SYSTEM\CurrentControlSet\Services\`+name, registry.QUERY_VALUE)
	if err != nil {
		return false
	}
	_ = key.Close()
	return true
}
