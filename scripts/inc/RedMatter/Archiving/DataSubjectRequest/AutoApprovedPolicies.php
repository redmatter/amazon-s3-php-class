<?php
/**
 * AutoApprovedPolicies.php
 *
 * PHP version 5
 *
 * @category  AutoApprovedPolicies.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */


namespace RedMatter\Archiving\DataSubjectRequest;


class AutoApprovedPolicies
{
    // list of auto-approval enabled policy-ids
    // [p1, p2]
    private $_autoApprovePolicies;

    // an array of array with user-id as key and array policy-ids he/she is a custodian of
    // [ u1 => [p1, p2], ... ]
    private $_policyCustodians;

    // an array that maps policy-id to custodians who has email enabled
    // [ p1 => [u1, u2], ...]
    private $_custodianPoliciesEmail;

    public function __construct($autoApprovePolicies, $policyCustodians, $custodianPoliciesEmail)
    {
        $this->_autoApprovePolicies = $autoApprovePolicies;
        $this->_policyCustodians = $policyCustodians;
        $this->_custodianPoliciesEmail = $custodianPoliciesEmail;
    }

    public function getPoliciesByUserId($userId)
    {
        return isset($this->_policyCustodians[$userId]) ?
            array_unique(array_merge($this->_autoApprovePolicies, $this->_policyCustodians[$userId])) :
            $this->_autoApprovePolicies;
    }

    public function getCustodiansForEmail($policies)
    {
        $custodians = [];
        foreach ($policies as $policy) {
            if (isset($this->_custodianPoliciesEmail[$policy])) {
                $custodians = array_merge($custodians, $this->_custodianPoliciesEmail[$policy]);
            }
        }

        return array_unique($custodians);
    }

    public static function Create($orgId)
    {
        $autoApprovePolicies = [];
        $policyCustodians = [];
        $custodianPoliciesEmail = [];

        /** @var \XPath $xpath */
        $xpath = \API::Get("/archiving/policy/{$orgId}/all");
        /** @var \DOMNodeList $policyNodes */
        $policyNodes = $xpath('/Archiving/Policies/Policy', true);
        foreach ($policyNodes as $policyNode)
        {
            $policyId = (int)$xpath('ID', false, $policyNode);

            if ('yes' === $xpath('AutoApproveDataSubjectRequest', false, $policyNode))
            {
                $autoApprovePolicies[] = $policyId;
            }

            $custodians = $xpath('DataCustodians/DataCustodian/@UserID', true, $policyNode);
            if ($custodians !== null)
            {
                /** @var \DOMNodeList $custodians */
                foreach ($custodians as $userId)
                {
                    $userId = (int)$userId->value;

                    if (!isset($policyCustodians[$userId]))
                    {
                        $policyCustodians[$userId] = [];
                    }

                    $policyCustodians[$userId][] = $policyId;
                }
            }

            $custodiansWithEmail = $xpath('DataCustodians[@Notify="yes"]/DataCustodian[@SendEmail="yes"]/@UserID', true, $policyNode);
            if ($custodiansWithEmail !== null)
            {
                $custodianPoliciesEmail[$policyId] = [];
                /** @var \DOMNodeList $custodiansWithEmail */
                foreach ($custodiansWithEmail as $userId)
                {
                    $custodianPoliciesEmail[$policyId][] = (int)$userId->value;
                }
            }
        }

        return new self($autoApprovePolicies, $policyCustodians, $custodianPoliciesEmail);
    }
}
