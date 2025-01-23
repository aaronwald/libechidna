
#pragma once

#define ECHIDNA_LOG_DEBUG(logger, fmt, ...) LOG_DEBUG(logger, fmt, ##__VA_ARGS__)
#define ECHIDNA_LOG_INFO(logger, fmt, ...) LOG_INFO(logger, fmt, ##__VA_ARGS__)
#define ECHIDNA_LOG_WARNING(logger, fmt, ...) LOG_WARN(logger, fmt, ##__VA_ARGS__)
#define ECHIDNA_LOG_ERROR(logger, fmt, ...) LOG_ERROR(logger, fmt, ##__VA_ARGS__)
