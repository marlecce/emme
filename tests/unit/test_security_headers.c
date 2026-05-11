#include <criterion/criterion.h>
#include <criterion/logging.h>
#include <string.h>
#include <stdio.h>
#include "config.h"
#include "http2_response.h"
#include "metrics.h"

Test(security_headers_config, default_config_is_enabled)
{
    ServerConfig config;
    int ret = load_config(&config, "config.yaml");
    
    cr_assert_eq(ret, 0, "Failed to load config");
    cr_assert_eq(config.security_headers.enabled, true, "Security headers should be enabled by default");
    cr_assert_gt(config.security_headers.header_count, 0, "Should have at least one security header");
}

Test(security_headers_config, parses_all_default_headers)
{
    ServerConfig config;
    int ret = load_config(&config, "config.yaml");
    
    cr_assert_eq(ret, 0, "Failed to load config");
    
    bool found_hsts = false;
    bool found_xcto = false;
    bool found_xfo = false;
    bool found_xxss = false;
    bool found_csp = false;
    bool found_rp = false;
    
    for (int i = 0; i < config.security_headers.header_count; i++) {
        if (strcmp(config.security_headers.headers[i].name, "Strict-Transport-Security") == 0)
            found_hsts = true;
        if (strcmp(config.security_headers.headers[i].name, "X-Content-Type-Options") == 0)
            found_xcto = true;
        if (strcmp(config.security_headers.headers[i].name, "X-Frame-Options") == 0)
            found_xfo = true;
        if (strcmp(config.security_headers.headers[i].name, "X-XSS-Protection") == 0)
            found_xxss = true;
        if (strcmp(config.security_headers.headers[i].name, "Content-Security-Policy") == 0)
            found_csp = true;
        if (strcmp(config.security_headers.headers[i].name, "Referrer-Policy") == 0)
            found_rp = true;
    }
    
    cr_assert_eq(found_hsts, true, "Should have HSTS header");
    cr_assert_eq(found_xcto, true, "Should have X-Content-Type-Options header");
    cr_assert_eq(found_xfo, true, "Should have X-Frame-Options header");
    cr_assert_eq(found_xxss, true, "Should have X-XSS-Protection header");
    cr_assert_eq(found_csp, true, "Should have CSP header");
    cr_assert_eq(found_rp, true, "Should have Referrer-Policy header");
}

Test(security_headers_config, hsts_header_value)
{
    ServerConfig config;
    int ret = load_config(&config, "config.yaml");
    
    cr_assert_eq(ret, 0, "Failed to load config");
    
    bool found = false;
    for (int i = 0; i < config.security_headers.header_count; i++) {
        if (strcmp(config.security_headers.headers[i].name, "Strict-Transport-Security") == 0) {
            cr_assert_str_eq(config.security_headers.headers[i].value, 
                           "max-age=31536000; includeSubDomains",
                           "HSTS should have correct value");
            found = true;
            break;
        }
    }
    
    cr_assert_eq(found, true, "HSTS header should be found");
}

Test(security_headers_config, csp_header_value)
{
    ServerConfig config;
    int ret = load_config(&config, "config.yaml");
    
    cr_assert_eq(ret, 0, "Failed to load config");
    
    bool found = false;
    for (int i = 0; i < config.security_headers.header_count; i++) {
        if (strcmp(config.security_headers.headers[i].name, "Content-Security-Policy") == 0) {
            cr_assert_str_eq(config.security_headers.headers[i].value, 
                           "default-src 'self'",
                           "CSP should have strict default value");
            found = true;
            break;
        }
    }
    
    cr_assert_eq(found, true, "CSP header should be found");
}

Test(security_headers_config, route_inherits_global_headers)
{
    ServerConfig config;
    int ret = load_config(&config, "config.yaml");
    
    cr_assert_eq(ret, 0, "Failed to load config");
    cr_assert_eq(config.route_count, 2, "Should have 2 routes");
    
    Route *api_route = &config.routes[1];
    cr_assert_str_eq(api_route->path, "/api/", "Should be API route");
    cr_assert_eq(api_route->inherit_global_headers, true, 
                "Route should inherit global headers by default");
}

Test(security_headers_cors, cors_config_parsed)
{
    ServerConfig config;
    int ret = load_config(&config, "config.yaml");
    
    cr_assert_eq(ret, 0, "Failed to load config");
    
    Route *api_route = NULL;
    for (int i = 0; i < config.route_count; i++) {
        if (strcmp(config.routes[i].path, "/api/") == 0) {
            api_route = &config.routes[i];
            break;
        }
    }
    
    cr_assert_not_null(api_route, "API route should exist");
    cr_assert_eq(api_route->cors.enabled, true, "CORS should be enabled for API route");
    cr_assert_str_eq(api_route->cors.allow_origin, "*", "Allow-Origin should be *");
    cr_assert_str_eq(api_route->cors.allow_methods, "GET, POST, OPTIONS", 
                    "Allow-Methods should match config");
    cr_assert_str_eq(api_route->cors.allow_headers, "Content-Type, Authorization",
                    "Allow-Headers should match config");
    cr_assert_eq(api_route->cors.allow_credentials, false, 
                "Allow-Credentials should be false");
    cr_assert_eq(api_route->cors.max_age_seconds, 86400, 
                "Max-Age should be 86400 seconds");
}

Test(security_headers_http2, h2_response_adds_security_headers)
{
    ServerConfig config;
    int ret = load_config(&config, "config.yaml");
    cr_assert_eq(ret, 0, "Failed to load config");
    
    Http2Response resp;
    h2_response_init(&resp);
    h2_response_set_status(&resp, 200, "OK");
    h2_response_set_content_type(&resp, "text/html");
    h2_response_set_body(&resp, "<html></html>", 13);
    
    long headers_before = resp.num_headers;
    h2_response_add_security_headers(&resp, &config.security_headers, NULL);
    
    cr_assert_gt(resp.num_headers, headers_before, 
                "Should add security headers to HTTP/2 response");
    cr_assert_eq(resp.num_headers, config.security_headers.header_count,
                "Should add all configured security headers");
}

Test(security_headers_http2, h2_response_adds_cors_headers)
{
    ServerConfig config;
    int ret = load_config(&config, "config.yaml");
    cr_assert_eq(ret, 0, "Failed to load config");
    
    Route *api_route = &config.routes[1];
    
    Http2Response resp;
    h2_response_init(&resp);
    h2_response_set_status(&resp, 200, "OK");
    h2_response_set_content_type(&resp, "application/json");
    h2_response_set_body(&resp, "{}", 2);
    
    h2_response_add_security_headers(&resp, &config.security_headers, &api_route->cors);
    
    bool found_acao = false;
    bool found_acam = false;
    bool found_acah = false;
    
    for (size_t i = 0; i < resp.num_headers; i++) {
        char name[64];
        snprintf(name, sizeof(name), "%s", (char*)resp.headers[i].name);
        if (strcmp(name, "Access-Control-Allow-Origin") == 0)
            found_acao = true;
        if (strcmp(name, "Access-Control-Allow-Methods") == 0)
            found_acam = true;
        if (strcmp(name, "Access-Control-Allow-Headers") == 0)
            found_acah = true;
    }
    
    cr_assert_eq(found_acao, true, "Should have Access-Control-Allow-Origin");
    cr_assert_eq(found_acam, true, "Should have Access-Control-Allow-Methods");
    cr_assert_eq(found_acah, true, "Should have Access-Control-Allow-Headers");
}

Test(security_headers_metrics, http2_response_adds_headers_successfully)
{
    ServerConfig config;
    int ret = load_config(&config, "config.yaml");
    cr_assert_eq(ret, 0, "Failed to load config");
    
    Http2Response resp;
    h2_response_init(&resp);
    h2_response_set_status(&resp, 200, "OK");
    h2_response_set_content_type(&resp, "text/html");
    h2_response_set_body(&resp, "<html></html>", 13);
    
    size_t headers_before = resp.num_headers;
    h2_response_add_security_headers(&resp, &config.security_headers, NULL);
    
    cr_assert_eq(resp.num_headers, headers_before + config.security_headers.header_count,
                "Should add all configured security headers to HTTP/2 response");
}

Test(security_headers_disabled, disabled_config_adds_no_headers)
{
    SecurityHeadersConfig disabled_config = {0};
    disabled_config.enabled = false;
    disabled_config.header_count = 0;
    
    Http2Response resp;
    h2_response_init(&resp);
    h2_response_set_status(&resp, 200, "OK");
    h2_response_set_body(&resp, "test", 4);
    
    long headers_before = resp.num_headers;
    h2_response_add_security_headers(&resp, &disabled_config, NULL);
    
    cr_assert_eq(resp.num_headers, headers_before, 
                "Should not add headers when disabled");
}

Test(security_headers_buffer, adds_headers_to_http1_buffer)
{
    ServerConfig config;
    int ret = load_config(&config, "config.yaml");
    cr_assert_eq(ret, 0, "Failed to load config");
    
    char buffer[1024];
    size_t len = 0;
    
    strcpy(buffer, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n");
    len = strlen(buffer);
    
    for (int i = 0; i < config.security_headers.header_count; i++) {
        int written = snprintf(buffer + len, sizeof(buffer) - len, 
                              "%s: %s\r\n",
                              config.security_headers.headers[i].name,
                              config.security_headers.headers[i].value);
        if (written > 0 && (size_t)written < sizeof(buffer) - len) {
            len += (size_t)written;
        }
    }
    
    cr_assert_not_null(strstr(buffer, "Strict-Transport-Security:"), 
                      "Buffer should contain HSTS header");
    cr_assert_not_null(strstr(buffer, "X-Content-Type-Options: nosniff"),
                      "Buffer should contain X-Content-Type-Options header");
    cr_assert_not_null(strstr(buffer, "X-Frame-Options: DENY"),
                      "Buffer should contain X-Frame-Options header");
}
