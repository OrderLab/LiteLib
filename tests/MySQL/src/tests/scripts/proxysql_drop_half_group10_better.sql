-- Better ProxySQL Configuration to drop 50% of requests to hostgroup 10
-- This approach uses error responses for specific query patterns to achieve ~50% drop
-- Connect to ProxySQL admin interface: mysql -u admin -padmin -h 127.0.0.1 -P6032

-- Clear existing query rules
DELETE FROM mysql_query_rules;

-- Method 1: Drop SELECT queries with even ID values (approximately 50%)
INSERT INTO mysql_query_rules (
    rule_id,
    active,
    match_pattern,
    destination_hostgroup,
    apply,
    error_msg,
    comment
) VALUES (
    1,
    1,
    '^SELECT.*WHERE.*id.*=.*[02468]$',
    10,
    1,
    'Request dropped by ProxySQL rule',
    'Drop SELECT queries with even ID values to hostgroup 10'
);

-- Method 2: Drop INSERT/UPDATE queries with even ID values (approximately 50%)
INSERT INTO mysql_query_rules (
    rule_id,
    active,
    match_pattern,
    destination_hostgroup,
    apply,
    error_msg,
    comment
) VALUES (
    2,
    1,
    '^(INSERT|UPDATE).*WHERE.*id.*=.*[02468]$',
    10,
    1,
    'Request dropped by ProxySQL rule',
    'Drop INSERT/UPDATE queries with even ID values to hostgroup 10'
);

-- Method 3: Drop queries containing specific patterns (approximately 50%)
INSERT INTO mysql_query_rules (
    rule_id,
    active,
    match_pattern,
    destination_hostgroup,
    apply,
    error_msg,
    comment
) VALUES (
    3,
    1,
    '^.*WHERE.*id.*LIKE.*[02468]$',
    10,
    1,
    'Request dropped by ProxySQL rule',
    'Drop queries with LIKE patterns containing even digits to hostgroup 10'
);

-- Apply the configuration
LOAD MYSQL QUERY RULES TO RUNTIME;
SAVE MYSQL QUERY RULES TO DISK;

-- Verify the configuration
SELECT rule_id, active, match_pattern, destination_hostgroup, apply, error_msg, comment 
FROM mysql_query_rules 
ORDER BY rule_id;

-- Show runtime configuration
SELECT rule_id, active, match_pattern, destination_hostgroup, apply, error_msg, comment 
FROM runtime_mysql_query_rules 
ORDER BY rule_id;
