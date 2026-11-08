"""HOST-ONLY recovery models. Pure bytes in/out; cannot open or write a device.

This models raw archive restoration onto initially blank storage. A receipt is
NOT authenticated and cannot establish physical ownership. Hardware needs an
independent persistent device-bound lease, a firmware writer fence, verified
source and guarded boot; this simulation does not supply any of those.
"""
import hashlib
from fm1_project_full_backup import SIZE,unpack_backup


def _digest(data):return hashlib.sha256(data).hexdigest()


def begin_blank_restore(archive, target, target_id):
    _, source=unpack_backup(archive)
    if not isinstance(target,bytes) or len(target)!=SIZE or target!=b'\xff'*SIZE:
        raise ValueError('destination is not provably blank')
    if not isinstance(target_id,str) or not target_id:
        raise ValueError('target identity required')
    # An in-memory simulator receipt is deliberately not an authorization.
    return {'source_sha256':_digest(source),'target_id':target_id,'model_only':True}


def restore_blank_step(archive,target,receipt,target_id,byte_budget=None):
    _,source=unpack_backup(archive)
    if not isinstance(target,bytes) or len(target)!=SIZE:
        raise ValueError('invalid destination')
    if receipt != {'source_sha256':_digest(source),'target_id':target_id,'model_only':True}:
        raise ValueError('restore receipt mismatch')
    if byte_budget is not None and (type(byte_budget) is not int or byte_budget<0):
        raise ValueError('invalid budget')
    # Refuse a target incompatible with one-way NOR writes before changing any
    # bytes. This is necessary but does not prove physical-sector ownership.
    if any((x & y)!=y for x,y in zip(target,source)):
        raise ValueError('destination contains incompatible data')
    candidate=bytearray(target);writes=0
    for i,value in enumerate(source):  # working pair is last in the archive
        if candidate[i]==value:continue
        if byte_budget is not None and writes==byte_budget:
            return bytes(candidate),False
        candidate[i] &= value
        writes+=1
    if bytes(candidate)!=source:raise ValueError('restore readback mismatch')
    return bytes(candidate),True
