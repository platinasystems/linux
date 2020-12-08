/**
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2020 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 * Copyright(c) 2018 Platina Systems, Inc.
 *
 * XETH driver, see Documentation/networking/xeth.txt
 */

#include <xeth_platina.h>

int xeth_encap = XETH_ENCAP_VLAN;
int xeth_base_xid = 3000;

static const struct xeth_config * const xeth_main_configs[] = {
	xeth_platina_configs,
	NULL,
};

static struct attribute *xeth_main_attrs[];

static const struct attribute_group xeth_main_attr_group = {
	.attrs = xeth_main_attrs,
};

static const struct attribute_group *xeth_main_attr_groups[] = {
	&xeth_main_attr_group,
	NULL,
};

static int xeth_main_ports(struct xeth_platform_priv *);

static int xeth_main(void *data)
{
	struct xeth_platform_priv *xpp = data;
	const int backlog = 3;
	struct socket *ln = NULL;
	struct sockaddr_un addr;
	char name[TASK_COMM_LEN];
	int n, err;


	xeth_flag_set(xpp, main_task);
	get_task_comm(name, current);
	allow_signal(SIGKILL);

	err = xeth_mux_register(xpp);
	if (err)
		return err;

	err = xeth_upper_register_drivers(xpp);
	if (err) {
		xeth_mux_unregister(xpp);
		return err;
	}

	err = xeth_main_ports(xpp);
	if (!err)
		err = xeth_qsfp_register_driver(xpp);
	if (err) {
		xeth_upper_unregister_drivers(xpp);
		xeth_mux_unregister(xpp);
		return err;
	}

	xpp->sb.conn = NULL;
	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	/* Note: This is an abstract namespace w/ addr.sun_path[0] == 0 */
	n = sizeof(sa_family_t) + 1 +
		scnprintf(addr.sun_path+1, UNIX_PATH_MAX-1, "%s",
			  xpp->config->name);

	err = sock_create_kern(current->nsproxy->net_ns,
			       AF_UNIX, SOCK_SEQPACKET, 0, &ln);
	if (err)
		goto xeth_main_exit;
	SOCK_INODE(ln)->i_mode &= ~(S_IRWXG | S_IRWXO);
	err = kernel_bind(ln, (struct sockaddr *)&addr, n);
	if (err)
		goto xeth_main_exit;
	err = kernel_listen(ln, backlog);
	if (err)
		goto xeth_main_exit;
	xeth_flag_set(xpp, sb_listen);
	while(!err && !kthread_should_stop() && !signal_pending(current)) {
		err = kernel_accept(ln, &xpp->sb.conn, O_NONBLOCK);
		if (err == -EAGAIN) {
			err = 0;
			msleep_interruptible(100);
			schedule();
			continue;
		}
		if (!err) {
			struct task_struct *sbrx;
			xeth_upper_reset_all_stats(xpp);
			xeth_flag_set(xpp, sb_connected);
			sbrx = xeth_sbrx_fork(xpp);
			if (sbrx) {
				xeth_sbtx_service(xpp);
				if (xeth_flag(xpp, sbrx_task)) {
					kthread_stop(sbrx);
					while (xeth_flag(xpp, sbrx_task)) {
						msleep_interruptible(100);
						schedule();
					}
				}
				xeth_upper_drop_all_carrier(xpp);
			}
			sock_release(xpp->sb.conn);
			xpp->sb.conn = NULL;
			xeth_flag_clear(xpp, sb_connected);
		}
	}
	rcu_barrier();
	xeth_flag_clear(xpp, sb_listen);
xeth_main_exit:
	xeth_qsfp_unregister_driver(xpp);
	xeth_upper_unregister_drivers(xpp);
	xeth_mux_unregister(xpp);
	if (ln)
		sock_release(ln);
	xeth_flag_clear(xpp, main_task);
	return err;
}

static int xeth_main_probe(struct platform_device *pdev)
{
	const struct xeth_config *config;
	struct xeth_platform_priv *xpp;
	int err;

	for (config = xeth_main_configs[0]; true; config++)
		if (!config)
			return -ENXIO;
		else if (!strcmp(config->name, pdev->name))
			break;

	err = -ENOMEM;
	xpp = devm_kzalloc(&pdev->dev, sizeof(*xpp), GFP_KERNEL);
	if (!xpp)
		goto xeth_main_probe_err;

	xpp->et_stat_names = devm_kzalloc(&pdev->dev,
					  ETH_GSTRING_LEN*config->max_et_stats,
					  GFP_KERNEL);
	if (!xpp->et_stat_names)
		goto xeth_main_probe_err;

	xpp->sb.rx.buf = devm_kzalloc(&pdev->dev,
				      XETH_SIZEOF_JUMBO_FRAME,
				      GFP_KERNEL);
	if (!xpp->sb.rx.buf)
		goto xeth_main_probe_err;

	xpp->pdev = pdev;
	xpp->config = config;

	err = onie_add_attrs(&pdev->dev);
	if (err)
		goto xeth_main_probe_err;

	err = devm_device_add_groups(&pdev->dev, xeth_main_attr_groups);
	if (err)
		goto xeth_main_probe_err;

	dev_set_drvdata(&pdev->dev, xpp);

	err = xpp->config->setup(xpp);
	if (err)
		goto xeth_main_probe_err;

	xpp->main = kthread_run(xeth_main, xpp, "%s", xpp->config->name);
	if (IS_ERR(xpp->main)) {
		err = PTR_ERR(xpp->main);
		xpp->main = NULL;
		goto xeth_main_probe_err;
	}
	return 0;

xeth_main_probe_err:
	if (xpp) {
		if (xpp->sb.rx.buf)
			devm_kfree(&pdev->dev, xpp->sb.rx.buf);
		if (xpp->et_stat_names)
			devm_kfree(&pdev->dev, xpp->et_stat_names);
		devm_kfree(&pdev->dev, xpp);
	}
	return err;
}

static int xeth_main_remove(struct platform_device *pdev)
{
	struct xeth_platform_priv *xpp = dev_get_drvdata(&pdev->dev);
	if (xpp && xpp->pdev == pdev) {
		if (xeth_flag(xpp, main_task)) {
			if (!IS_ERR_OR_NULL(xpp->main))
				kthread_stop(xpp->main);
			xpp->main = NULL;
			while (xeth_flag(xpp, main_task)) ;
		}
	}
	return 0;
}

static int xeth_main_ports(struct xeth_platform_priv *xpp)
{
	char ifname[IFNAMSIZ];
	u64 ea;
	u32 o, xid;
	u16 port;
	u8 subport;
	int qsfp_bus, err;
	void (*setup)(struct ethtool_link_ksettings *);

	for (port = 0; port < xpp->config->n_ports; port++) {
		size_t provision = xpp->config->provision ?
			xpp->config->provision(port) : 1;

		xpp->config->port_label(xpp, ifname, port);

		switch (provision) {
		case 1:
			xid = xpp->config->top_xid - port;
			ea = xpp->base_mac;
			if (ea)
				ea += port;
			setup = xpp->config->port_setup;
			qsfp_bus = xpp->config->qsfp_bus[port];

			err = xeth_upper_new_port(xpp, ifname, xid, ea,
						  setup, qsfp_bus);
			if (err)
				return err;
			break;
		case 2:
		case 4:
			strcat(ifname, "-#");
			for (subport = 0; subport < provision; subport++) {
				o = xpp->config->n_ports * subport;
				xid = xpp->config->top_xid - port - o;
				ea = xpp->base_mac;
				if (ea)
					ea += port + o;
				ifname[strlen(ifname)-1] = '1' + subport;
				setup = subport ? xpp->config->subport_setup :
					xpp->config->port_setup;
				qsfp_bus = subport ? -1 :
					xpp->config->qsfp_bus[port];
				err = xeth_upper_new_port(xpp, ifname, xid, ea,
							  setup, qsfp_bus);
				if (err)
					return err;
			}
			break;
		}
	}
	return 0;
}

static ssize_t xeth_main_show_stat_name(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct xeth_platform_priv *xpp = dev_get_drvdata(dev);
	if (!xpp->n_et_stats)
		return 0;
	return strlcpy(buf, xpp->et_stat_names +
		       ((xpp->n_et_stats - 1) * ETH_GSTRING_LEN),
		       ETH_GSTRING_LEN);
}

static ssize_t xeth_main_store_stat_name(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t sz)
{
	struct xeth_platform_priv *xpp = dev_get_drvdata(dev);
	int i, j = 0;

	if (!sz || buf[0] == '\n') {
		xpp->n_et_stats = 0;
		return sz;
	}
	if (xpp->n_et_stats >= xpp->config->max_et_stats)
		return -EINVAL;
	for (i = xpp->n_et_stats * ETH_GSTRING_LEN;
	     j < ETH_GSTRING_LEN;
	     i++, j++)
		if (buf[j] == '\n' || j == sz) {
			xpp->et_stat_names[i] = '\0';
			break;
		} else {
			xpp->et_stat_names[i] = buf[j];
		}
	xpp->n_et_stats++;
	return sz;
}

struct device_attribute xeth_main_attr_stat_name =
	__ATTR(stat_name, 0644,
	       xeth_main_show_stat_name, xeth_main_store_stat_name);

static struct attribute *xeth_main_attrs[] = {
	&xeth_main_attr_stat_name.attr,
	NULL,
};

static const struct platform_device_id xeth_main_device_ids[] = {
	xeth_platina_device_ids,
	{},
};

MODULE_DEVICE_TABLE(platform, xeth_main_device_ids);

static struct platform_driver xeth_main_platform_driver = {
	.driver		= { .name = KBUILD_MODNAME },
	.probe		= xeth_main_probe,
	.remove		= xeth_main_remove,
	.id_table	= xeth_main_device_ids,
};

static int __init xeth_main_init(void)
{
	return platform_driver_register(&xeth_main_platform_driver);
}
module_init(xeth_main_init);

static void __exit xeth_main_exit(void)
{
	platform_driver_unregister(&xeth_main_platform_driver);
}
module_exit(xeth_main_exit);

MODULE_DESCRIPTION("mux proxy netdevs with a remote switch");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Platina Systems");
MODULE_VERSION(xeth_version);
MODULE_SOFTDEP("pre: nvmem-onie");
