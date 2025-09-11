#!/bin/bash

# Script to enable/disable ProxySQL configuration to drop 50% of requests to hostgroup 10
# Usage: ./apply_proxysql_drop_config.sh [enable|disable|status]

PROXYSQL_CMD="/usr/local/mysql/bin/mysql -u admin -padmin -h 127.0.0.1 -P6032"
CONFIG_FILE="/users/toga/cascade_mysql/tests/MySQL/src/tests/scripts/proxysql_drop_half_group10_better.sql"

show_usage() {
    echo "Usage: $0 [enable|disable|status]"
    echo ""
    echo "Commands:"
    echo "  enable  - Enable dropping 50% of requests to hostgroup 10 (error-based)"
    echo "  disable - Disable the drop configuration"
    echo "  status  - Show current configuration status"
    echo ""
    echo "Examples:"
    echo "  $0 enable   # Enable request dropping"
    echo "  $0 disable  # Disable request dropping"
    echo "  $0 status   # Show current status"
}

enable_drop() {
    echo "Enabling ProxySQL configuration to drop 50% of requests to hostgroup 10 (error-based method)"
    
    # Apply the configuration
    $PROXYSQL_CMD < $CONFIG_FILE
    
    if [ $? -eq 0 ]; then
        echo "Configuration enabled successfully!"
        show_status
    else
        echo "Failed to enable configuration. Please check ProxySQL is running and accessible."
        exit 1
    fi
}

disable_drop() {
    echo "Disabling ProxySQL drop configuration..."
    
    # Disable the rule
    $PROXYSQL_CMD -e "UPDATE mysql_query_rules SET active=0 WHERE rule_id=1; LOAD MYSQL QUERY RULES TO RUNTIME;"
    
    if [ $? -eq 0 ]; then
        echo "Configuration disabled successfully!"
        show_status
    else
        echo "Failed to disable configuration. Please check ProxySQL is running and accessible."
        exit 1
    fi
}

show_status() {
    echo ""
    echo "Current ProxySQL query rules:"
    $PROXYSQL_CMD -e "SELECT rule_id, active, match_pattern, destination_hostgroup, apply, delay, comment FROM mysql_query_rules ORDER BY rule_id;"
    echo ""
    echo "Runtime query rules:"
    $PROXYSQL_CMD -e "SELECT rule_id, active, match_pattern, destination_hostgroup, apply, delay, comment FROM runtime_mysql_query_rules ORDER BY rule_id;"
}

# Main script logic
case "${1:-}" in
    enable)
        enable_drop
        ;;
    disable)
        disable_drop
        ;;
    status)
        show_status
        ;;
    *)
        show_usage
        exit 1
        ;;
esac
