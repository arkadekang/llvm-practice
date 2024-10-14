add_llvm_pass_plugin(DecomposeGEP
  DecomposeGEPPass.cpp

  LINK_COMPONENTS
  Core
  Support
  TransformUtils
  Analysis
  )
