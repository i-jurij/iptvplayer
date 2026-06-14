#pragma once

enum class ErrorCode {
  OK,
  FileNotFound,
  InvalidIndex,
  CurlInitError,
  NetworkError,
  HttpError,
  ParseError,
  InvalidHeader,
  DUPLICATE,
  PermissionDenied,
  IoError,
  UnsafePath,
  Unknown
};
