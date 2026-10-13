package main

type serviceInfo struct {
	Installed bool
	UserLevel bool
	Running   bool
	Stopped   bool
}

func serviceStatusText() string {
	info := detectService()
	if !info.Installed {
		return "未安装"
	}
	kind := "系统服务"
	if info.UserLevel {
		kind = "用户服务"
	}
	switch {
	case info.Running:
		return kind + "（运行中）"
	case info.Stopped:
		return kind + "（已停止）"
	default:
		return kind
	}
}
