{
  "targets": [
    {
      "target_name": "xattrAsync",
      "sources": [
        "src/xattr-async.cpp"
      ],
      "include_dirs": [
        "<!(node -e \"require('nan')\")"
      ]
    }
  ]
}
